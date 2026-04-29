#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <endian.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

_Static_assert(BYTE_ORDER == LITTLE_ENDIAN);

#define BOM_UTF8 0x00bfbbefu

int main()
{
	errno = 0;
	int rc = 0;
	struct stat statbuf = {};
	char const *chat = "chat.txt";
	rc = stat(chat, &statbuf);
	if (-1 == rc) {
		fprintf(stderr, "%s%s%s", "error: status of file:", chat, "\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	fprintf(stdout, "chat-size: %lu\n", statbuf.st_size);

	int fd = open(chat, O_RDONLY);
	uint64_t const start_chat = lseek(fd, 0, SEEK_SET);
	uint64_t const end_chat = lseek(fd, 0, SEEK_END);
	uint64_t const len_chat = (end_chat - start_chat);
	if (0 == len_chat) {
		fprintf(stderr, "%s", "error: empty chat\n");
		_exit(1);
	}
	else if (statbuf.st_size != len_chat) {
		fprintf(stderr, "%s", "error: unexpected chat size mismatch\n");
		fprintf(stderr, "error: stat.filesize: %lu seek.filesize: %lu", statbuf.st_size, len_chat);
		_exit(1);
	}

	uint64_t const pagesz = sysconf(_SC_PAGESIZE);
	uint64_t const len_mmap = (len_chat & 1)
		? (((len_chat + 0) + (pagesz - 1)) & ~(pagesz - 1))
		: (((len_chat + 1) + (pagesz - 1)) & ~(pagesz - 1));

	fprintf(stdout, "chat-length (bytes): %lu\n", len_chat);
	fprintf(stdout, "mmap-size (bytes): %lu\n", len_mmap);

	void *bufchat = mmap(NULL, len_mmap, PROT_READ, MAP_PRIVATE, fd, 0);
	if (!bufchat) {
		fprintf(stderr, "%s", "error: chat memory mapping failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	close(fd);
	if (-1 == (rc = madvise(bufchat, len_mmap, MADV_WILLNEED))) {
		fprintf(stderr, "%s", "error: mmap fast access failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	uint64_t count = 0;
	char unsigned *txt = bufchat;
	if (BOM_UTF8 == ((*((uint32_t*) txt)) & 0x00ffffff)) {
		txt += 3;
		count += 3;
	}
	else if ((0xfffeu == (*((uint16_t*) txt))) || (0xfeffu == (*((uint16_t*) txt)))) {
		fprintf(stderr, "%s", "error: unsupported utf-16\n");
		_exit(1);
	}
	/* excludes emojis and other non-ASCII characters from the chat */
	while (len_chat > count) {
		if (0x80u > (*txt)) {
			if (((*txt) >= 0x41u) && ((*txt) < 0x5au)) {
				fprintf(stdout, "%c", (((*txt) - 0x41u) + 0x61u));
			}
			else {
				fprintf(stdout, "%c", *txt);
			}
			txt += 1;
			count += 1;
		}
		else if (0xc2 > (*txt))  {
			fprintf(stderr, "%s", "error: invalid utf-8 prefix\n");
			_exit(1);
		}
		else if (0xe0u > (*txt)) {
			txt += 2;
			count += 2;
		}
		else if (0xf0u > (*txt)) {
			txt += 3;
			count += 3;
		}
		else {
			txt += 4;
			count += 4;
		}
	}
	if (len_chat != count) {
		fprintf(stderr, "%s", "error: bytes read and filesize mismatch\n");
		_exit(1);
	}
	else {
		fprintf(stdout, "%s %lu %s", "bytes-read:", count, "\n");
	}
	return 0;
}
