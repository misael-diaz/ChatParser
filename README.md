# ChatParser
A tool for parsing plain-text WhatsApp chat-exports and migrating them to a SQLite database.

*Bringing the precision of High Performance Computing to Businesses.*

## Motivation
I am crafting this tool for my own business. The general idea is to use this tool to store the conversations with my clients (with their consent of course) in a database for storage and analytics.  My intention is to query WhatsApp chats from the command-line instead of using the WhatsApp limited builtin search feature. Anyone who tries to run a business on their own with WhatsApp's basic tier understands its limitations. Another strong reason for ingesting the chats into SQL is that this allows me to index them (fast search) and structure the data to find trends hidden in the data to tailor my business.

So this tool requires me to export the chats with my clients in plain text one by one. I don't have a problem with that. Running a business in solo mode means being organized, disciplined, and strategic.

All I am going to say is that if you know the tech you can develop your own tool according to your needs, no need to wait for the sought after feature to be released by Meta. And I am not using AI to generate the code for this tool; instead I am using it to discuss edge cases and to close knowledge-gaps that I may have (not without doing the research myself for verification). This means that I am the systems architect and the builder as well of this tool. I rather have a complete understanding of the tools that my business depends on.

## Compile

This is a zero-dependency util that can be compiled with GCC:

```sh
gcc -O2 main.c -o chat-parser.bin
```

You may want to experiment with other optimization levels.

## Run

The chat-parser is a Unix filter, so converting a UTF-8 text file into ASCII is as simple as doing as piping the contents of the chat into the parser in this way:

```sh
cat chat.txt | chat-parser.bin
```

The util does not output anything else other than the transliterated text.
Note that the util must be in your `PATH` for that to work, otherwise use:

```sh
cat chat.txt | ./chat-parser.bin
```

and here it is assumed that the chat and the util are in the current working directory.

If you want to suppress warnings (due to unknown command-line arguments):

```sh
cat chat.txt | ./chat-parser.bin 2>/dev/null
```

you can redirect them to the null device, for warnings are written to the standard error stream.

The only command-line argument that this tool understand is the help argument:

```sh
./chat-parser.bin --help
```

and this shows the example usage that you see in this section.

## Development Status

This section is devoted to log the development of this application to keep a comprehensive history beyond what one can usually find from git-commit logs. I talk about edge cases, problems and their solutions, design and performance considerations, etc.

Quick access to the development logs:

- [Day 1: Exploring Unicode](#day-1)
- [Day 2: Unicode to ASCII Transliteration](#day-2)
- [Day 3: Transliterator Portability](#day-3)
- [Day 4: Exploring Timestamp Encodings](#day-4)
- [Day 5: Timestamp Spatial Mapping](#day-5)
- [Day 6: Forging a Unix Filter](#day-6)
- [Day 7: Fixes](#day-7)


### Day 1
The drive of the first day is to experiment with the idea: Can I parse [Unicode](https://www.w3schools.com/charsets/ref_html_utf8.asp) encoded text file without using a library?


Why bother at all? First reason, most web pages use Unicode encoding and this makes sense because of internationalization. People around the world want to interact with content in their native language. The web we know today would not exist without it. So the first reason is to have a better idea of an encoding that we take for granted; but it is one of the most important achievements for the web world. The other reason is that this would be a great opportunity for me to learn how to deal with Unicode at a low level.

Is it not this project about developing a performant application for my business? Yes indeed but if there is something that I can learn from the act of writing the code myself, then that is worth my time. Because this is an experience that I can take with me to the next project. The business applications of today rely on layers upon layers of code, my application is just one level above the GNU/Linux platform; meaning that it is closer to the metal and so it will outperform most existing business applications (especially vibe coded applications) that address this problem.

### Lessons
This section is devoted to mentioning the important lessons that were derived from experimenting with my code. I used GDB to inspect the stream of bytes and also the `xxd` command-line tool to look at the contents in hex.

- Parsing Unicode is not like parsing a stream of bytes as in ASCII. However knowing ASCII codes helps understand how to downsize Unicode encoded data to ASCII. The general idea is that all characters but ASCII characters are preceded by a prefix byte that indicates I belong to this table (in a loose sense).  For example the hex prefix code for [Latin Extended A](https://www.w3schools.com/charsets/ref_utf_latin_extended_a.asp) characters is `0xC3`. The first character that belongs to that table is `À` and two bytes are needed to encode it, `0xC3` and `0x80`. A similar pattern can be seen for [Latin Extended B](https://www.w3schools.com/charsets/ref_utf_latin_extended_b.asp) characters, the first character in that table is `ƀ`, and it also requires two bytes to encode it, `0xC6` and `0x80`.

- It is easy to differentiate Latin Extended A and B characters from ASCII for two reasons. The first reason is the absence of the prefix byte and the second is that ASCII characters must be the asymmetric range `0x00` to `0x80`, meaning that the end value is not inclusive, for only 7-bits of the 8-bits in a byte are needed to represent ASCII. If you go back to the first characters of Latin Extended A and B you will see that `0x80` follows the prefix byte. This is clever because there's no way for a parser that treats bytes as belonging to ASCII characters to print non-ASCII characters; instead nothing would be shown. This realization also tells me that after downsizing the stream of encoded Unicode characters to ASCII no byte should be greater than or equal to `0x80`. And that is a great way to validate the conversion.

### Achievements
- Gets the file status of the chat for getting the modification file timestamp
- Maps the chat to memory for performance on Linux (via `mmap`)
- Implement basic parsing of UTF-8 files (takes into account presence of Byte Order Mask (BOM))
- Writes the entire chat to stdout while excluding non-ASCII characters (no emojis, accented characters, etc.)

#### Observations
- To simplify our life later down the road we are assuming now that the code is going to run on Little Endian (LE) CPUs, if that's not the case it won't compile. This should not be a problem because modern CPUs are LE.
- Printing the chat on standard output shall be removed in a future revision.
- The filtering of non-ASCII characters filters out accented characters from words written in Spanish.
- Accented characters are to be replaced with their respective non-accented version (á -> a) for simplifying queries.

#### Considerations
- Use simple conditionals to map the accented characters and take into account that the Latin characters that we care about are mostly in sequence so a range-base check reduces the amount of comparisons.
- Maybe you want to store the chat messages in lowercase also so that we don't end up with mixed case words even if they are present in the original messages.

#### Testing
Adding the Byte Order Mask (BOM) to a UTF-8 that does not have any can be done via this command:

```sh
printf '\xEF\xBB\xBF' > chat.txt
```

To verify that the file has only three bytes:

```sh
du -b file.txt
```

Then you can concat the original chat file to obtain a UTF-8 file with BOM to test that the code does not break if there's a BOM.

We were able to parse and print an entire WhatsApp chat consisting of just ASCII characters.

### Day 2
The objective of the day was to transform the Unicode text to ASCII lowercase while mapping the common
accented characters to their ASCII counterparts. The reason for storing the chats in lowercase is that
it simplifies the querying code (because all the text has the same case).


#### Lessons Learned
Unintended unaligned access in commit [1f792ba](https://github.com/misael-diaz/ChatParser/blob/1f792ba653cda313e52e90a843b1addb2fa8339f/main.c#L100) can happen because of the direct casting. The solution to this problem was to construct the 16-bit integer with bitwise operations `uint16_t const value = ((txt[1] << 8) | txt[0]);`. The referenced memory in text is also unsigned so the resulting code is not going to set bits. And this is important because we want the tool to handle these efficiently. This issue was pointed out to me by AI and I verified that the optimized binary (after modification) used a fast instruction for loading the 16-bit integer to memory `movzwl  (%rbx), %eax`; the instruction is a zero-extended meaning that the 16-bit pattern is moved to a 32-bit register the hi 16-bits set to zero which is what we want. We can get away with the casting at the beginning of the pointer returned by `mmap` because it is going to be paged aligned, no need to construct values with bitwise operations.


#### Achievements
- Maps Latin Extended characters to their ASCII lowercase counterparts.
- The mapped characters comprise the most used accented characters used in Spanish.
- These are two-byte characters, this is why we cast them to 16-bit integers with confidence.
- We use unsigned integer casting because the raw data is interpreted as unsigned as well, this matters when extending from 16-bit to 32-bit in the code that compares the encoding.
- Even though we are hardcoding the Unicode encoding in hex the code is still readable because the next dev would see the mapping clearly.
- Considerations from the previous day were all addressed.


#### Considerations
- You may want to copy the modified data to a new buffer.
- Verify that the buffer contains ASCII characters.
- Check for embedded null characters in the new buffer (none expected) except at the end of the chat because we would use an anonymous mapping in this case (zero initialized by the Linux Kernel for us).
- You may want to convert ASCII in the asymmetric range [0, 32] to whitespace (decimal 32) and also you may have to either complain if you find decimal 127 in the resulting text or convert it to zero.


#### Testing
I was able to read WhatsApp chats, the output met the expectations: lowercase and plain ASCII text.


### Day 3
Commit the transliterated Unicode into ASCII to a destination memory buffer and print it on the console.

#### Lessons Learned
- **Portability**: The main source file at commit [60b7a0c](https://github.com/misael-diaz/ChatParser/commit/60b7a0c037ed654bccc4624d1929ccebd28711d1) has an static assert that checks if the CPU architecture is LE, if that is not the case the compilation is aborted. This check of course makes the code non-portable to BE CPUs. The reason for adding that check was due to few lines of code where we cast the chat data to a 16-bit or 32-bit integer. By instead assembling the data in the expected order with bitwise operations we obtain the same result but the compiler is smart enough to assemble the integer in a single `mov` instruction when optimizations are enabled (for example, when compiling with `-O2`). Therefore by assembling the integers instead of casting we were able to remove the static assert (because it is no longer needed) and as result of that the code is now portable; it may be compiled in both LE and BE CPUs and the output should be the same. The current version of the code does not have the CPU architecture assertion (portable). By caring about the details we fulfill our commitment to leverage the HPC precision to develop performant tools.

#### Achievements
Extended the code to efficiently (sequential access) write chat messages to an anonymous memory map.

#### Considerations
- **Normalization**: get rid of control sequences from the ASCII so that it won't interfere with a web-based dashboard later down the road.
- **Performance**: Do the transliteration, normalization, and validation, on the same loop for performance. Running these on separate loops does not improve but detriment performance because it is probably evicted from the CPU cache. Profiling should confirm this in the end.

#### Testing
From the user's perspective the code does the same, for the chat messages look as expected without emojis and without Latin accented characters commonly used in Spanish. However, from my perspective I know that instead of printing an ASCII character at a time (slow) there's only one call to `fprintf` to print the chat messages. And also I know that the ASCII characters are in the expected range (not greater than or equal to decimal 128).

### Day 4
On this day I have mostly focused on researching how to deal with timestamps in a timezone agnostic way.  The problem is that the timestamps in the WhatsApp chat messages are based with respect to the geographical timezone and the compute server has a different timezone. This means that the application is reading timestamps from WhatsApp that correspond to a different timezone than that of the underlying platform.  Of course Meta has no reason to include the timezone data in the timestamps, if you want them you might have to register to the WhatsApp API.


#### Achievements
Laid out a plan to handle missing timezone data from the WhatsApp chat timestamps. The plan boils down to setting the timezone environment variable before parsing the chat data and leverage the system util [`mktime()`](https://man7.org/linux/man-pages/man3/mktime.3p.html) to obtain 64-bit encoded timestamps. The 64-bit encoding is equivalent to the number of seconds that have elapsed since the Unix Epoch (`1970-01-01 00:00:00 +0000 (UTC)`). Commit [acd6f4c]( https://github.com/misael-diaz/ChatParser/commit/acd6f4c08dee4e2144c35de10091819f30d6dea6) experiments with the timestamp encoding and it looks promising.


**Advantages**

The advantages of the timestamp encoding solution are outlined here:

- **Storage**: Storing the timestamps as 64-bit integers in the database is efficient, it is preferable to storing them as a string which could introduce interpretation errors down the rode. One also has to consider that if the timestamp is stored as a string one needs to include the timezone data as well.

- **Sorting**: Querying the database with respect to time often involves sorting and that means that the cost of sorting is tied to the number of operations needed to compare timestamps. From an algorithmic standpoint, it is clear that comparing integers is faster than comparing strings. 
 
- **Duplicate Timestamps**: Timestamp duplicates are quite common in WhatsApp chat messages because WhatsApp exports chats with timestamps with a resolution of minutes.  To differentiate them all that we need to do is to add a suitable time-interval (seconds) between them.  An elaborate solution would be to estimate the time it takes to write the text based on the number of characters but a simple constant addition will do for practical purposes.


#### Considerations
- Need to take into account how SQLite handles timestamps given as 64-bit integers. This involves reading SQLite's documentation.

- Address probable edge cases not yet considered in this exploratory phase. For example, what if in the future a client wants a web-based dashboard application. Would we need an API that would handle the edge cases to keep the frontend code strictly for presenting the content, rather than to deal with data processing?


#### Testing
I also experimented with using simple conditionals instead of regular expressions (regex) for locating the timestamps in the chat. The choice for simple logic has been favored over regex because it allows me to establish a baseline for benchmarking if the code ever needs regex down the road.  Another reason for sticking with simple logic is that we can look at the underlying patterns (or code execution branches) that the regex would take.  It is also probable that even with a regex we will need additional logic to handle the construction of the helper time structure `struct tm` for handling hours. Note that the time struct expects hours in the (exclusive) range from 0 to 24; whereas, the WhatsApp chat provides hours in the (inclusive) range from 0 to 12 (AM | PM).

The experimental version of the timestamp locator code has been committed to commit [f6eb650]( https://github.com/misael-diaz/ChatParser/commit/f6eb650895e8ebb0d2f0a59e1cfaa0e17a600e0b ). Testing has confirmed that it can detect typical timestamps from WhatsApp Business chats.

#### Conclusions
It is reassuring to find out that Chromium's JavaScript [V8 engine](https://github.com/v8/v8/blob/bd3ed01527c850cd5268fb11ecd4cc9576333d5c/src/base/platform/platform-cygwin.cc#L86)&mdash;the engine that powers the most popular browsers&mdash;reaches out for the same system time utilities that I used for handling timezone offsets.

### Day 5
The clear goal of the day was to start working on the mapping of the timestamps as offsets with respect to the base address of the output data (the ASCII that results from transliterating Unicode).

#### Lessons Learned

- **Advantages of K&R Coding Conventions**: By not cuddling an `else` or `else if` next to closing braces I was able to write the code with more ease than by following the opposite convention. Programming style is something that evolves and is cemented with practice. So I learned this by experimenting with both and now I have decided to use the K&R style for this one.


- **Refactoring**: Even though I saw opportunities to refactor the code that I was working on, I reaffirmed that it is best to postpone that until the repetition becomes a fingerprint of the underlying logic. This is why there are no functions defined at this point.


- **Handling mktime() Errors**: Even if [`errno`](https://man7.org/linux/man-pages/man3/errno.3.html) is set during a call to [`mktime()`](https://man7.org/linux/man-pages/man3/errno.3.html) that alone does not necessarily means that it failed to encode the time into a 64-bit integer. Only when `mktime()` returns `-1` there's an error that we should be wary of.


#### Achievements

- **Mapped Chat Timestamps**: Spatial mapping of timestamps from the base address pointer. This is so that we can construct the message text and the user id more easily in another codeblock. Doing more would make it harder to maintain the code, so we can afford to do the other data mappings (chat content and userid) in separate loops.


- **Encoded Timestamps**: Encoding the timestamp in a 64-bit integer that represents the elapsed number of seconds since the Unix Epoch. The encoding takes into account the timezone of the chat data. This means that we have succeeded in implementing a timezone agnostic representation of the timestamps. So it won't matter if the database is hosted in Alaska or anywhere else, timezones won't be a problem.


#### Testing

We verified that omitting the [`tzset()`](https://man7.org/linux/man-pages/man3/tzset.3.html) changes the encoding values for the timestamps.


### Day 6

It was not intentional but my research and curiosity drove me to make the tool useful for scripting and automation tools. The fact that the chat data was hardcoded into a file in the current working directory bothered me&mdash;this is not how Unix tools empowers my computing experience. So I decided to experiment with the idea of high-performance IO when reading from a pipe (a high-performance Unix filter). The problem is that we don't know ahead of time how large the map should be, so the solution was to let it grow as needed as a C++'s standard vector. The hint came from Quake's engine source code, for the idSoftware developers used [`mremap()`](https://github.com/id-Software/Quake-2/blob/372afde46e7defc9dd2d719a1732b8ace1fa096e/linux/q_shlinux.c#L52) to resize the virtual address space for the legendary First Person Shooter (FPS) game when running on Linux. 

#### Lessons Learned

Main lessons learned while forging the code into a Unix filter.

- Learned that [`mremap()`](https://man7.org/linux/man-pages/man2/mremap.2.html) is very efficient at growing the virtual address space, and if we are clever enough to use offsets instead of pointers we can let the Linux Kernel shift it somewhere else so that the resulting memory section that we get is contiguous. Of course I used offsets so that if the base address changes my code does not crash. When the Linux Kernel moves the map elsewhere it does so efficiently without incurring on copying the data, this is why this is a zero-copy reallocator.

- Rewrote some clever hacks into boring code to make sure that the debugging experience is better. For example, calling a function, doing assignment, and checking for the return value in an if-expression is a clever hack that should be avoided. It is better to call the function before checking the value in an if-expression. This sounds simple but writing production code sometimes mean writing code defensively and write it for the next developer
(which can be me in a couple of months).
- Must check that `argc` is greater or equal to one even if on Linux that `argv[0]` holds the name of the executable because there are edge cases. For example, in Linux it is allowed to call [`execve()`](https://man7.org/linux/man-pages/man2/execve.2.html) with `argv[]` set to `NULL`.


- Learned that [`lseek()`](https://man7.org/linux/man-pages/man2/lseek.2.html) can be used to make sure that we are dealing with a pipe (not seekable devices). And that this is the ultimate check, though it is also useful to check for the file status mode. Both approaches are used in the code for robustness.

#### Achievements

- Forged the code so that it behaves as a Unix filter, which is the most probable use for this utility.

- Used [`read()`](https://man7.org/linux/man-pages/man2/read.2.html) and [`write()`](https://man7.org/linux/man-pages/man2/write.2.html) calls for initializing the memory map that holds the Unicode text and for outputting the transliterated ASCII text to the console. On Linux this is as fast as one can do synchronous IO operations.

- Separates the experimental code from the filtering code to make it easier to use for scripts and automation tools. The experimental code is too verbose and interferes with the output and that is not desirable.

- Allocating an extra page serves the purpose of making the text data being interpreted as a string by functions that expect the null character `\0` such as the `fprintf` family of functions. It also provides space for experimenting with the spatial mapping of the chat data, which is relevant for ingesting the data into SQLite.

#### Testing

Chat files larger than the initial map size were used to check that virtual address doubles as needed.


### Day 7

On this day I put the Unicode to ASCII transliterator to the test by having it process a WhatsApp group chat (real conversation data in Spanish). That's when I discovered two problems with my code. Found characters less that the Feed Line (FL) and the timestamp detection logic encountered an unexpected character where AM or PM should be.


#### Lessons Learned

- **Defensive Programming**: The defensive programming code that was already in place was instrumental for detecting these errors. As soon as I ran the code the problems were detected and displaying the source file name and problematic file as part of the error report streamlined the troubleshooting.


- **Real Data**: Using real data early to check the code for correctness saved me the trouble of dealing with bogus data at the database.


- **Reading Documentation**: Reading the documentation helped me to build the defensive programming code and also helped me to interpret the errors and know how to fix them. Knowing your stack beats vibe coding. Knowing the stack is part of what it makes a Tech Founder.

#### Achievements

- **Fixed Embedded Nulls**: The problem was the code that folds extended Latin characters to ASCII. The destination pointer would be incremented whether or not folding took place and this had the side effect of embedding null characters `\0`. The appearance of the null character comes from the fact that the code uses an anonymous memory mapping `mmap()` as a destination buffer, which is zero initialized by the Linux Kernel before it is handed to the application for security purposes.


- **Fixed Octal Base Interpretation**: The other problem was puzzling at first, the `strtol()` function set the `endptr` to a number and that number was `8`. The reason for this is that the code was using the special zero value for the base. The tool can interpret numeric data in a string in various bases. The zero value is tricky because the number may be interpreted as octal, decimal, or hexadecimal depending on the pointed memory region. Therefore, when the pointed area has `08` the util switches to octal because of the leading zero but then stops at `8` because it is invalid (`0` - `7` are legit octal values). By explicitly telling `strtol()` to use base `10` the time values were handled properly.


- **Fixed Edge Case**: The tool `mktime()` to convert `struct tm` data into a 64-bit integer expects the time data (seconds, minutes, hour, etc.) to be bounded to a range. Particularly, hour values must be in the asymmetric range `[0, 24)`. This means that hours values in the range `12:00pm - 12:59pm` should not increment the hour value. This issue was also found by the defensive programming code. The fix was to increment the hour value by 12 after from `1:00pm` to `11:59pm`.
