# pocsag-encoder

Encode pocsag messages to dump to a file. This project is designed to make it
easier to understand how pocsag works, with a heavily-commented implementation.

Takes input as a series of lines, one line per message.

Message format is as follows:

```
address:message
```

where address is an integer, and message is contents to be encoded.

Adds a random delay to the output feed of 1 to 10 seconds by default. This
is configurable in pocsag.c near the bottom of the file by the MIN\_DELAY and
MAX\_DELAY defines.

`pocsag` reads from stdin and writes signed 16 bit little-endian samples to stdout.


# Example Usage

```bash
# encode two messages, and verify multimon can decode them
printf '1:hello\n9:world' | pocsag | multimon-ng -c -a POCSAG512 -q -

# encode a message to a file (raw, 22050Hz PCM S16LE)
printf '11:good evening' > transmission.raw
```

# Compilation

`pocsag` doesn't rely on any dependencies but the C standard libraries. Use
`make` to compile, or run your own C compiler manually. Feel free to
`sudo make install` if you want.
