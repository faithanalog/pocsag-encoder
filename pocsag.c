#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>

//Check out main() at the bottom of the file
//You can modify MIN_DELAY and MAX_DELAY to fit your needs.


//Check out https://en.wikipedia.org/wiki/POCSAG
//Also see http://www.itu.int/dms_pubrec/itu-r/rec/m/R-REC-M.584-2-199711-I!!PDF-E.pdf
//They'll be handy when trying to understand this stuff.

//The sync word exists at the start of every batch.
//A batch is 16 words, so a sync word occurs every 16 data words.
#define SYNC 0x7CD215D8

//The idle word is used as padding before the address word, and at the end
//of a message to indicate that the message is finished. Interestingly, the
//idle word does not have a valid CRC code, while the sync word does.
#define IDLE 0x7A89C197

//One frame consists of a pair of two words
#define FRAME_SIZE 2

//One batch consists of 8 frames, or 16 words
#define BATCH_SIZE 16

//The preamble comes before a message, and is a series of alternating
//1,0,1,0... bits, for at least 576 bits. It exists to allow the receiver
//to synchronize with the transmitter
#define PREAMBLE_LENGTH 576


//These bits appear as the first bit of a word, 0 for an address word and
//one for a data word
#define FLAG_ADDRESS 0x000000
#define FLAG_MESSAGE 0x100000


//The last two bits of an address word's data represent the data type
//0x3 for text, and 0x0 for numeric.
#define FLAG_TEXT_DATA 0x3
#define FLAG_NUMERIC_DATA = 0x0;

//Each data word can contain 20 bits of text information. Each character is
//7 bits wide, ASCII encoded. The bit order of the characters is reversed from
//the normal bit order; the most significant bit of a word corresponds to the
//least significant bit of a character it is encoding. The characters are split
//across the words of a message to ensure maximal usage of all bits.
#define TEXT_BITS_PER_WORD 20

//As mentioned above, characters are 7 bit ASCII encoded
#define TEXT_BITS_PER_CHAR 7

//Length of CRC codes in bits
#define CRC_BITS 10

//The CRC generator polynomial
#define CRC_GENERATOR 0b11101101001

/**
 * Calculate the CRC error checking code for the given word.
 * Messages use a 10 bit CRC computed from the 21 data bits.
 * This is calculated through a binary polynomial long division, returning
 * the remainder.
 * See https://en.wikipedia.org/wiki/Cyclic_redundancy_check#Computation
 * for more information.
 */
uint32_t crc(uint32_t inputMsg) {
    //Align MSB of denominatorerator with MSB of message
    uint32_t denominator = CRC_GENERATOR << 20;

    //Message is right-padded with zeroes to the message length + crc length
    uint32_t msg = inputMsg << CRC_BITS;

    //We iterate until denominator has been right-shifted back to it's original value.
    for (int column = 0; column <= 20; column++) {
        //Bit for the column we're aligned to
        int msgBit = (msg >> (30 - column)) & 1;

        //If the current bit is zero, we don't modify the message this iteration
        if (msgBit != 0) {
            //While we would normally subtract in long division, we XOR here.
            msg ^= denominator;
        }

        //Shift the denominator over to align with the next column
        denominator >>= 1;
    }

    //At this point 'msg' contains the CRC value we've calculated
    return msg & 0x3FF;
}


/**
 * Calculates the even parity bit for a message.
 * If the number of bits in the message is even, return 0, else return 1.
 */
uint32_t parity(uint32_t x) {
    //Our parity bit
    uint32_t p = 0;

    //We xor p with each bit of the input value. This works because
    //xoring two one-bits will cancel out and leave a zero bit.  Thus
    //xoring any even number of one bits will result in zero, and xoring
    //any odd number of one bits will result in one.
    for (int i = 0; i < 32; i++) {
        p ^= (x & 1);
        x >>= 1;
    }
    return p;
}

/**
 * Encodes a 21-bit message by calculating and adding a CRC code and parity bit.
 */
uint32_t encodeCodeword(uint32_t msg) {
    uint32_t fullCRC = (msg << CRC_BITS) | crc(msg);
    uint32_t p = parity(fullCRC);
    return (fullCRC << 1) | p;
}

/**
 * ASCII encode a null-terminated string as a series of codewords, written
 * to (*out). Returns the number of codewords written. Caller should ensure
 * that enough memory is allocated in (*out) to contain the message
 *
 * initial_offset indicates which word in the current batch the function is
 * beginning at, so that it can insert SYNC words at appropriate locations.
 */
uint32_t encodeASCII(uint32_t initial_offset, char* str, uint32_t* out) {
    //Number of words written to *out
    uint32_t numWordsWritten = 0;

    //Data for the current word we're writing
    uint32_t currentWord = 0;

    //Nnumber of bits we've written so far to the current word
    uint32_t currentNumBits = 0;

    //Position of current word in the current batch
    uint32_t wordPosition = initial_offset;

    while (*str != 0) {
        unsigned char c = *str;
        str++;
        //Encode the character bits backwards
        for (int i = 0; i < TEXT_BITS_PER_CHAR; i++) {
            currentWord <<= 1;
            currentWord |= (c >> i) & 1;
            currentNumBits++;
            if (currentNumBits == TEXT_BITS_PER_WORD) {
                //Add the MESSAGE flag to our current word and encode it.
                *out = encodeCodeword(currentWord | FLAG_MESSAGE);
                out++;
                currentWord = 0;
                currentNumBits = 0;
                numWordsWritten++;

                wordPosition++;
                if (wordPosition == BATCH_SIZE) {
                    //We've filled a full batch, time to insert a SYNC word
                    //and start a new one.
                    *out = SYNC;
                    out++;
                    numWordsWritten++;
                    wordPosition = 0;
                }
            }
        }
    }

    //Write remainder of message
    if (currentNumBits > 0) {
        //Pad out the word to 20 bits with zeroes
        currentWord <<= 20 - currentNumBits;
        *out = encodeCodeword(currentWord | FLAG_MESSAGE);
        out++;
        numWordsWritten++;

        wordPosition++;
        if (wordPosition == BATCH_SIZE) {
            //We've filled a full batch, time to insert a SYNC word
            //and start a new one.
            *out = SYNC;
            out++;
            numWordsWritten++;
            wordPosition = 0;
        }
    }

    return numWordsWritten;
}

/**
 * An address of 21 bits, but only 18 of those bits are encoded in the address
 * word itself. The remaining 3 bits are derived from which frame in the batch
 * is the address word. This calculates the number of words (not frames!)
 * which must precede the address word so that it is in the right spot. These
 * words will be filled with the idle value.
 */
uint32_t addressOffset(uint32_t address) {
    return (address & 0x7) * FRAME_SIZE;
}

/**
 * Encode a full text POCSAG transmission addressed to (address).
 * (*message) is a null terminated C string.
 * (*out) is the destination to which the transmission will be written.
 */
void encodeTransmission(int address, char* message, uint32_t* out) {

    
    //Encode preamble
    //Alternating 1,0,1,0 bits for 576 bits, used for receiver to synchronize
    //with transmitter
    for (int i = 0; i < PREAMBLE_LENGTH / 32; i++) {
        *out = 0xAAAAAAAA;
        out++;
    }

    uint32_t* start = out;

    //Sync
    *out = SYNC;
    out++;

    //Write out padding before adderss word
    int prefixLength = addressOffset(address);
    for (int i = 0; i < prefixLength; i++) {
        *out = IDLE;
        out++;
    }

    //Write address word.
    //The last two bits of word's data contain the message type
    //The 3 least significant bits are dropped, as those are encoded by the
    //word's location.
    *out = encodeCodeword( ((address >> 3) << 2) | FLAG_TEXT_DATA);
    out++;

    //Encode the message itself
    out += encodeASCII(addressOffset(address) + 1, message, out);


    //Finally, write an IDLE word indicating the end of the message
    *out = IDLE;
    out++;
    
    //Pad out the last batch with IDLE to write multiple of BATCH_SIZE + 1
    //words (+ 1 is there because of the SYNC words)
    size_t written = out - start;
    size_t padding = (BATCH_SIZE + 1) - written % (BATCH_SIZE + 1);
    for (int i = 0; i < padding; i++) {
        *out = IDLE;
        out++;
    }
}

/**
 * Calculates the length in words of a text POCSAG message, given the address
 * and the number of characters to be transmitted.
 */
size_t textMessageLength(int address, int numChars) {
    size_t numWords = 0;

    //Padding before address word.
    numWords += addressOffset(address);

    //Address word itself
    numWords++;

    //numChars * 7 bits per character / 20 bits per word, rounding up
    numWords += (numChars * TEXT_BITS_PER_CHAR + (TEXT_BITS_PER_WORD - 1))
                    / TEXT_BITS_PER_WORD;

    //Idle word representing end of message
    numWords++;

    //Pad out last batch with idles
    numWords += BATCH_SIZE - (numWords % BATCH_SIZE);

    //Batches consist of 16 words each and are preceded by a sync word.
    //So we add one word for every 16 message words
    numWords += numWords / BATCH_SIZE;

    //Preamble of 576 alternating 1,0,1,0 bits before the message
    //Even though this comes first, we add it to the length last so it
    //doesn't affect the other word-based calculations
    numWords += PREAMBLE_LENGTH / 32;

    return numWords;
}

//=== ALRIGHTY, time for some stuff completely unrelated to POCSAG itself. ===
//We need to be able to encode this data as PCM audio for multimon-ng to decode.
//It expects input at a sample rate of 22050 Hz, but that does not divide well
//into any of the valid POCSAG baud rates of 512, 1200, or 2400. So instead,
//we're going to encode data at a sample rate of 38400 Hz, which is an evenly
//divisible by all of those baud rates, and then "resample" to 22050 Hz  with no
//interpolation whatsoever. Audio engineers would hate me here...


//Samples are 16 bit signed PCM audio samples.
//A negative value represents 1, while a positive value represents 0
//No value represents a pause in the signal

#define SYMRATE 38400

size_t pcmTransmissionLength(
        uint32_t sampleRate,
        uint32_t baudRate,
        size_t transmissionLength) {
    //32 bits per word * (sampleRate / baudRate) samples.
    //Each sample is 16 bits, but we encode to an 8 bit array.
    return transmissionLength * 32 * sampleRate / baudRate * 2;
}

/**
 * sampleRate: Sample rate of output data
 * baudRate: Baud rate ouf output data
 * (*transmission): POCSAG-encoded message to transmit
 * transmissionLength: length in words of the transmission
 * (*out): Destination for output audio samples. Should be at least
 *         (transmissionLength * 32 * sampleRate / baudRate * 2) bytes in size.
 */
void pcmEncodeTransmission(
        uint32_t sampleRate,
        uint32_t baudRate,
        uint32_t* transmission,
        size_t transmissionLength,
        uint8_t* out) {

    //Number of times we need to repeat each bit to achieve SYMRATE
    int repeatsPerBit = SYMRATE / baudRate;

    //Initial buffer for samples before resampling occurs
    int16_t* samples =
        (int16_t*) malloc(sizeof(int16_t) * transmissionLength * 32 * repeatsPerBit);

    
    //Encode transmission as an audio signal
    
    //Pointer to samples we can modify in the loop
    int16_t* psamples = samples;
    for (size_t i = 0; i < transmissionLength; i++) {

        //Word to encode
        uint32_t val = *(transmission + i);

        for (int bitNum = 0; bitNum < 32; bitNum++) {

            //Encode from most significant to least significant bit
            int bit = (val >> (31 - bitNum)) & 1;
            int16_t sample;
            if (bit == 0) {
                sample = 32767 / 2;
            } else {
                sample = -32767 / 2;
            }

            //Repeat as many times as we need for the current baudrate
            for (int r = 0; r < repeatsPerBit; r++) {
                *psamples = sample;
                psamples++;
            }
        }
    }

    //Resample to 22050 sample rate
    size_t outputSize =
        pcmTransmissionLength(sampleRate, baudRate, transmissionLength);
    for (size_t i = 0; i < outputSize; i += 2) {
        //Round to closest index in input data which corresponds to output index
        int16_t inSample = *(samples + (i / 2) * SYMRATE / sampleRate);
        
        //Write little-endian
        *(out + i + 0) = (inSample & 0xFF);
        *(out + i + 1) = ((inSample >> 8) & 0xFF);
    }

    //And we're done! Delete our temporary buffer
    free(samples);
}


#define SAMPLE_RATE 22050
#define BAUD_RATE 512

#define MIN_DELAY 1
#define MAX_DELAY 10

int main() {
    //Read in lines from STDIN.
    //Lines are in the format of address:message
    //The program will encode transmissions for each message, writing them
    //to STDOUT. It will also encode a rand amount of silence between them,
    //from 1-10 seconds in length, to act as a simulated "delay".
    char line[65536];
    srand(time(NULL));
    for (;;) {

        if (fgets(line, sizeof(line), stdin) == NULL) {
            //Exit on EOF
            return 0;
        }

        // fgets() returns the line *with* the trailing \n, which I don't want.
        // To remove that, set the null terminator to be one earlier than it is
        // if the string ends with a newline.
        size_t line_length = strlen(line);
        if (line_length == 0) {
            return 0;
        }

        if (line[line_length - 1] == '\n') {
            line_length--;
            line[line_length] = 0;
            if (line_length == 0) {
                continue;
            }
        }

        // Be nice and ignore a trailing \r too, though, how did that get here?
        if (line[line_length - 1] == '\r') {
            line_length--;
            line[line_length] = 0;
            if (line_length == 0) {
                continue;
            }
        }

        size_t colonIndex = 0;
        for (size_t i = 0; i < sizeof(line); i++) {
            if (line[i] == 0) {
                fprintf(stderr, "Malformed Line!\n");
                return 1;
            }
            if (line[i] == ':') {
                colonIndex = i;
                break;
            }
        }

        uint32_t address = (uint32_t) strtol(line, NULL, 10);

        // Largest 21-bit address
        if (address > 2097151) {
            fprintf(stderr, "Address exceeds 21 bits: %u\n", address);
            return 1;
        }

        char* message = line + colonIndex + 1;

        size_t messageLength = textMessageLength(address, strlen(message));

        uint32_t* transmission =
            (uint32_t*) malloc(sizeof(uint32_t) * messageLength);

        encodeTransmission(address, message, transmission);

        size_t pcmLength =
            pcmTransmissionLength(SAMPLE_RATE, BAUD_RATE, messageLength);

        uint8_t* pcm =
            (uint8_t*) malloc(sizeof(uint8_t) * pcmLength);

        pcmEncodeTransmission(
                SAMPLE_RATE, BAUD_RATE, transmission, messageLength, pcm);

        //Write as series of little endian 16 bit samples
        fwrite(pcm, sizeof(uint8_t), pcmLength, stdout);

        free(transmission);
        free(pcm);

        //Generate rand amount of silence. Silence is a sample with
        //a value of 0.
        
        //1-10 seconds
        size_t silenceLength = rand() % (SAMPLE_RATE * (MAX_DELAY - MIN_DELAY)) + MIN_DELAY;

        //Since the values are zero, endianness doesn't matter here
        uint16_t* silence =
            (uint16_t*) malloc(sizeof(uint16_t) * silenceLength);

        bzero(silence, sizeof(uint16_t) * silenceLength);
        fwrite(silence, sizeof(uint16_t), silenceLength, stdout);
        free(silence);
    }
}
