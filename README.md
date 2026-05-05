# ChatParser
A tool for parsing plain-text WhatsApp chat-exports and migrating them to a SQLite database.

*Bringing the precision of High Performance Computing to Businesses.*

## Motivation
I am crafting this tool for my own business. The general idea is to use this tool to store the conversations with my clients (with their consent of course) in a database for storage and analytics.  My intention is to query WhatsApp chats from the command-line instead of using the WhatsApp limited builtin search feature. Anyone who tries to run a business on their own with WhatsApp's basic tier understands its limitations. Another strong reason for ingesting the chats into SQL is that this allows me to index them (fast search) and structure the data to find trends hidden in the data to tailor my business.

So this tool requires me to export the chats with my clients in plain text one by one. I don't have a problem with that. Running a business in solo mode means being organized, disciplined, and strategic.

All I am going to say is that if you know the tech you can develop your own tool according to your needs, no need to wait for the sought after feature to be released by Meta. And I am not using AI to generate the code for this tool; instead I am using it to discuss edge cases and to close knowledge-gaps that I may have (not without doing the research myself for verification). This means that I am the systems architect and the builder as well of this tool. I rather have a complete understanding of the tools that my business depends on.

## Development Status

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

The experimental version of the timestamp locator code has been committed to commit [f6eb650](
https://github.com/misael-diaz/ChatParser/commit/f6eb650895e8ebb0d2f0a59e1cfaa0e17a600e0b 
). Testing has confirmed that it can detect typical timestamps from WhatsApp Business chats.

#### Conclusions
It is reassuring to find out that Chromium's JavaScript [V8 engine](https://github.com/v8/v8/blob/bd3ed01527c850cd5268fb11ecd4cc9576333d5c/src/base/platform/platform-cygwin.cc#L86)&mdash;the engine that powers the most popular browsers&mdash;reaches out for the same system time utilities that I used for handling timezone offsets.
