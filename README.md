# ChatParser
A tool for parsing plain-text WhatsApp chat-exports and migrating them to a SQLite database.

*Bringing the precision of High Performance Computing to Businesses.*

## Motivation
I am crafting this tool for my own business. The general idea is to use this tool to store the
conversations with my clients (with their consent of course) in a database for storage and analytics.
My intention is to query WhatsApp chats from the command-line instead of using the WhatsApp limited builtin
search feature. Anyone who tries to run a business on their own with WhatsApp's basic tier understands its
limitations.

So this tool requires me to export the chats with my clients in plain text one by one. I don't have a
problem with that. Running a business in solo mode means being organized, disciplined, and strategic.

All I am going to say is that if you know the tech you can develop your own tool according to your needs,
no need to wait for the needed future to be released by Meta. And I am not using AI to generate the code
for this tool; instead I am using it to discuss edge cases. This means that I am the systems architect and
the builder as well, for I rather have a complete understanding of every aspect of my own tools.

## Development Status

### Day 1

### Achievements
- Gets the file status of the chat for getting the modification file timestamp
- Maps the chat to memory for performance on Linux (via `mmap`)
- Impl basic parsing of UTF-8 files (takes into account presence of Byte Order Mask (BOM))
- Writes the entire chat to stdout while excluding non-ASCII characters (no emojis, accented characters, etc.)

#### Observations
- To simplify our life later down the road we are assuming now that the code is going to run on Little Endian
(LE) CPUs, if that's not the case it won't compile. This should not be a problem because modern CPUs are LE.
- Printing the chat on standard output shall be removed in a future revision.
- The filtering of non-ASCII characters filters out accented characters from words written in Spanish.
- Accented characters are to be replaced with their respective non-accented version (á -> a) for simplifying
  queries.

#### Considerations
- Use simple conditionals to map the accencted characters and take into account that the Latin characters
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
