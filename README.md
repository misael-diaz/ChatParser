# ChatParser
A tool for parsing plain-text WhatsApp chat-exports and migrating them to a SQLite database.

*Bringing the precision of High Performance Computing to Businesses.*

## Motivation
I am crafting this tool for my own business. The general idea is to use this tool to store the
conversations with my clients (with their consent of course) in a database for storage and analytics.
My intention is to query WhatsApp chats from the command-line instead of using the WhatsApp limited builtin
search feature. Anyone who tries to run a business on their own with WhatsApp's basic tier understands its
limitations. Another strong reason for ingesting the chats into SQL is that this allows me to index them
(fast search) and structure the data to find trends hidden in the data to tailor my business.

So this tool requires me to export the chats with my clients in plain text one by one. I don't have a
problem with that. Running a business in solo mode means being organized, disciplined, and strategic.

All I am going to say is that if you know the tech you can develop your own tool according to your needs,
no need to wait for the needed future to be released by Meta. And I am not using AI to generate the code
for this tool; instead I am using it to discuss edge cases. This means that I am the systems architect and
the builder as well, for I rather have a complete understanding of every aspect of my own tools.

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
- To simplify our life later down the road we are assuming now that the code is going to run on Little Endian
(LE) CPUs, if that's not the case it won't compile. This should not be a problem because modern CPUs are LE.
- Printing the chat on standard output shall be removed in a future revision.
- The filtering of non-ASCII characters filters out accented characters from words written in Spanish.
- Accented characters are to be replaced with their respective non-accented version (á -> a) for simplifying
  queries.

#### Considerations
- Use simple conditionals to map the accented characters and take into account that the Latin characters
  that we care about are mostly in sequence so a range-base check reduces the amount of comparisons.
- Maybe you want to store the chat messages in lowercase also so that we don't end up with mixed case
  words even if they are present in the original messages.

#### Testing
Adding the Byte Order Mask (BOM) to a UTF-8 that does not have any can be done via this command:

```sh
printf '\xEF\xBB\xBF' > chat.txt
```

To verify that the file has only three bytes:

```sh
du -b file.txt
```

Then you can concat the original chat file to obtain a UTF-8 file with BOM to test that the code does
not break if there's a BOM.

We were able to parse and print an entire WhatsApp chat consisting of just ASCII characters.
