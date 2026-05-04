#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>

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

	void *srcbuf = mmap(NULL, len_mmap, PROT_READ, MAP_PRIVATE, fd, 0);
	if (!srcbuf) {
		fprintf(stderr, "%s", "error: source chat memory mapping failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	void *dstbuf = mmap(NULL, len_mmap, PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
	if (!dstbuf) {
		fprintf(stderr, "%s", "error: destination chat memory mapping failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	close(fd);
	if (-1 == (rc = madvise(srcbuf, len_mmap, MADV_WILLNEED))) {
		fprintf(stderr, "%s", "error: src mmap fast access failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	if (-1 == (rc = madvise(dstbuf, len_mmap, MADV_WILLNEED))) {
		fprintf(stderr, "%s", "error: dest mmap sequential access failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}

	uint64_t count = 0;
	uint64_t len_txt = 0;
	char unsigned *txt = srcbuf;
	char unsigned *dst = dstbuf;
	uint32_t const head_utf8 = 0x00ffffff & (
		(txt[3] << 24) |
		(txt[2] << 16) |
		(txt[1] <<  8) |
		(txt[0] <<  0)
	);
	uint16_t const head_utf16 = ((txt[1] << 8) | txt[0]);
	if (BOM_UTF8 == head_utf8) {
		txt += 3;
		count += 3;
	}
	else if ((0xfffeu == head_utf16) || (0xfeffu == head_utf16)) {
		fprintf(stderr, "%s", "error: unsupported utf-16\n");
		_exit(1);
	}
	/* excludes emojis and other non-ASCII characters from the chat */
	while (len_chat > count) {
		if (0x80u > (*txt)) {
			if (((*txt) < 0x0au)) {
				*dst = 0x20u;
			}
			else if (((*txt) >= 0x0bu) && ((*txt) < 0x20u)) {
				*dst = 0x20u;
			}
			else if (((*txt) >= 0x41u) && ((*txt) < 0x5bu)) {
				*dst = (((*txt) - 0x41u) + 0x61u);
			}
			else if ((0x7fu == (*txt))) {
				*dst = 0x20u;
			}
			else {
				*dst = *txt;
			}
			txt += 1;
			dst += 1;
			count += 1;
			len_txt += 1;
		}
		else if (0xc2 > (*txt))  {
			fprintf(stderr, "%s", "error: invalid utf-8 prefix\n");
			_exit(1);
		}
		else if (0xe0u > (*txt)) {
			uint16_t const value = ((txt[1] << 8) | txt[0]);
			if ((value >= 0x80c3u) && (value < 0x86c3u)) {
				*dst = 'a';
			}
			else if ((value >= 0x88c3u) && (value < 0x8cc3u)) {
				*dst = 'e';
			}
			else if ((value >= 0x8cc3u) && (value < 0x90c3u)) {
				*dst = 'i';
			}
			else if ((value >= 0x92c3u) && (value < 0x97c3u)) {
				*dst = 'o';
			}
			else if ((value >= 0x99c3u) && (value < 0x9ec3u)) {
				fprintf(stdout, "%c", 'u');
				*dst = 'u';
			}
			else if ((value >= 0xa0c3u) && (value < 0xa6c3u)) {
				*dst = 'a';
			}
			else if ((value >= 0xa8c3u) && (value < 0xacc3u)) {
				*dst = 'e';
			}
			else if ((value >= 0xacc3u) && (value < 0xb0c3u)) {
				*dst = 'i';
			}
			else if ((value == 0xb1c3u)) {
				*dst = 'n';
			}
			else if ((value >= 0xb2c3u) && (value < 0xb7c3u)) {
				*dst = 'o';
			}
			else if ((value >= 0xb9c3u) && (value < 0xbdc3u)) {
				*dst = 'u';
			}
			txt += 2;
			dst += 1;
			len_txt += 1;
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
		fprintf(stdout, "%s %lu %s", "bytes-kept:", len_txt, "\n");
	}

	if (-1 == (rc = munmap(srcbuf, len_mmap))) {
		fprintf(stderr, "%s", "error: unmapping chat failed\n");
		if (errno) {
			fprintf(stderr, "%s\n", strerror(errno));
		}
		_exit(1);
	}
	srcbuf = NULL;

#if DEVBUILD
	dst = dstbuf;
	for (int i = 0; i != len_txt; ++i) {
		if (*dst >= 0x80u) {
			fprintf(stderr, "%s", "error: unicode to ascii transliteration failed\n");
			_exit(1);
		}
	}
	fprintf(stdout, "%s", (char*) dstbuf);
#endif
	return 0;
}
