/*
Resources:
http://www.music.mcgill.ca/~ich/classes/mumt306/StandardMIDIfileformat.html
https://www.fourmilab.ch/webtools/midicsv/
https://www.cs.cmu.edu/~music/cmsip/readings/davids-midi-spec.htm
https://github.com/OneLoneCoder/olcPixelGameEngine/blob/master/Videos/OneLoneCoder_PGE_MIDI.cpp
http://www.gweep.net/~prefect/eng/reference/protocol/midispec.html
https://www.eecs.umich.edu/courses/eecs373/Lec/StudentF18/MIDI%20Presentation.pdf

Usage:
gcc main.c -o main -Wall
./main music.mid music.csv
*/


#include <stdio.h>
#include <stdint.h>
#include <string.h>


enum EventType
{
    EventNoteOff = 0x80,
    EventNoteOn = 0x90,
    EventKeyPressure = 0xA0,
    EventControllerChange = 0xB0,
    EventProgramChange = 0xC0,
    EventChannelPressure = 0xD0,
    EventPitchBend = 0xE0,
    EventSystemExclusive = 0xF0
};

enum MetaEventType
{
    MetaSequence = 0x00,
    MetaText = 0x01,
    MetaCopyright = 0x02,
    MetaTrackName = 0x03,
    MetaInstrumentName = 0x04,
    MetaLyrics = 0x05,
    MetaMarker = 0x06,
    MetaCuePoint = 0x07,
    MetaChannelPrefix = 0x20,
    MetaEndOfTrack = 0x2F,
    MetaSetTempo = 0x51,
    MetaSMPTEOffset = 0x54,
    MetaTimeSignature = 0x58,
    MetaKeySignature = 0x59,
    MetaSequencerSpecific = 0x7F
};

enum Error
{
    InvalidHeaderChunk,
    InvalidTrackChunk,
    PotentialBufferOverflow
};

/*
struct MidiNote
{
    uint8_t key;
    uint8_t velocity;
    uint32_t time;
    uint32_t duration;
};

struct MidiEvent
{
    enum EventType event;
    uint8_t key;
    uint8_t velocity;
    uint8_t delta_tick;
};
*/


uint32_t swap32(uint32_t n)
{
    return (n << 24 & 0xFF000000) | (n << 8 & 0x00FF0000) | (n >> 8 & 0x0000FF00) | (n >> 24 & 0x000000FF);
}

uint16_t swap16(uint16_t n)
{
    return (n << 8 & 0xFF00) | (n >> 8 & 0x00FF);
}

uint32_t read_value(FILE *src)
{
    uint8_t buffer = 0x80;
    uint32_t value = 0;
    while (buffer & 0x80) {
        buffer = fgetc(src);
        value = value << 7 | (buffer & 0x7F);
    }

    return value;
}


void fprintn(FILE *dest, FILE *src, size_t n)
{
    uint8_t c;
    fputc('"', dest);

    while (n--) {
        c = fgetc(src);
        // `c` is printable or it doesn't belong to ASCII.
        switch ((c >= ' ') << 1 | (c == '"')) {
        case 3:
            // Quote characters embedded within strings are represented by `two` consecutive quotes.
            fputc(c, dest);
        case 2:
            fputc(c, dest);
            break;
        default:
            // 3 digits octal representation of non-printable characters prefixed with `\`.
            fprintf(dest, "\\%03o", c);
        }
    }

    fputc('"', dest);
}


struct HeaderChunk
{
    char chunk_id[4];
    uint32_t length;
    uint16_t format;
    uint16_t track_chunks;
    uint16_t time_division;
};

struct HeaderChunk create_header_chunk(FILE *src)
{
    struct HeaderChunk hc;
    fread(&hc.chunk_id, 1, 4, src);
    fread(&hc.length, 4, 1, src);
    fread(&hc.format, 2, 1, src);
    fread(&hc.track_chunks, 2, 1, src);
    fread(&hc.time_division, 2, 1, src);

    hc.length = swap32(hc.length);
    hc.format = swap16(hc.format);
    hc.track_chunks = swap16(hc.track_chunks);
    hc.time_division = swap16(hc.time_division);

    return hc;
}

uint8_t midi_to_csv(FILE *midi, FILE *csv)
{
    // TODO: Can't handle small invalid input from stdin.

    size_t timestamp = 0;

    uint32_t
    delta_time,
    chunk_length,
    event_length,
    tempo = 0,
    BPM = 0;

    // Could have used a simple buffer instead of so many variables, but it was too unintuitive...
    uint8_t
    previous_status,
    status,
    channel,
    end_of_track,
    pitch,
    velocity,
    key,
    pressure,
    controller,
    value,
    preset,
    bend_LSB,
    bend_MSB,
    type;

    char buffer[64];
    FILE *error_stream = stderr;

    char chunk_id[4];

    struct HeaderChunk header = create_header_chunk(midi);

    if (strncmp(header.chunk_id, "MThd", 4) || (header.format == 0 && header.track_chunks != 1)) {
        return InvalidHeaderChunk;
    }

    fprintf(
        csv, "0, 0, Header, %hu, %hu, %hu\n",
        header.format, header.track_chunks, header.time_division
    );

    for (size_t ntrack = 1; ntrack <= header.track_chunks; ++ntrack) {
        end_of_track = 0;
        timestamp = 0;

        fread(chunk_id, 1, 4, midi);

        // Track chunk length in bytes. Skip this number of bytes to get to next track.
        fread(&chunk_length, 4, 1, midi);
        chunk_length = swap32(chunk_length);
        fprintf(error_stream, "%u %u\n", ntrack, chunk_length);

        if (strncmp(chunk_id, "MTrk", 4)) {
            return InvalidTrackChunk;
        }

        fprintf(csv, "%lu, %lu, Start_track\n", ntrack, timestamp);

        while (!feof(midi) && !end_of_track) {
            // All MIDI events contain a timecode, and a status byte.

            // Delta time in "ticks" from the previous event.
            // Could be 0 if two events happen simultaneously.
            delta_time = read_value(midi);
            timestamp += delta_time;

            // Read first byte of message, this could be the status byte, or it could not...
            status = fgetc(midi);

            // Handle MIDI Running Status
            if (status < 0x80) {
                status = previous_status;
                fseek(midi, -1, SEEK_CUR);
            }

            previous_status = status;
            channel = status & 0x0F;

            // Non-meta events
            switch (status & 0xF0) {
            case EventNoteOn:
                pitch = fgetc(midi),
                velocity = fgetc(midi);
                fprintf(
                    csv, "%lu, %lu, Note_on_c, %hhu, %hhu, %hhu\n",
                    ntrack, timestamp, channel, pitch, velocity
                );
                break;

            case EventNoteOff:
                pitch = fgetc(midi);
                velocity = fgetc(midi);
                fprintf(
                    csv, "%lu, %lu, %s, %hhu, %hhu, %hhu\n",
                    ntrack, timestamp, (velocity) ? "Note_off_c" : "Note_on_c", channel, pitch, velocity
                );

            case EventKeyPressure:
                key = fgetc(midi);
                pressure = fgetc(midi);
                fprintf(
                    csv, "%lu, %lu, Poly_aftertouch_c, %hhu, %hhu, %hhu\n",
                    ntrack, timestamp, channel, key, pressure
                );
                break;

            case EventControllerChange:
                controller = fgetc(midi);
                value = fgetc(midi);
                fprintf(
                    csv, "%lu, %lu, Control_c, %hhu, %hhu, %hhu\n",
                    ntrack, timestamp, channel, controller, value
                );
                break;

            case EventProgramChange:
                preset = fgetc(midi);
                fprintf(
                    csv, "%lu, %lu, Program_c, %hhu, %hhu\n",
                    ntrack, timestamp, channel, preset
                );
                break;

            case EventChannelPressure:
                // `monophonic` or `channel` aftertouch applies to the Channel as a whole,
                // not individual note numbers on that channel.
                pressure = fgetc(midi);
                fprintf(
                    csv, "%lu, %lu, Channel_aftertouch_c, %hhu, %hhu\n",
                    ntrack, timestamp, channel, pressure
                );
                break;

            case EventPitchBend:
                bend_LSB = fgetc(midi);
                bend_MSB = fgetc(midi);
                fprintf(
                    csv, "%lu, %lu, Pitch_bend_c, %hhu, %hhu\n",
                    ntrack, timestamp, channel, (bend_MSB & 0x7F) << 7 | (bend_LSB & 0x7F)
                );
                break;

            case EventSystemExclusive:
                // Storing vendor-specific information to be transmitted to that vendor's products.
                // SystemExclusive events and meta events cancel any running status which was in effect.
                // Meta events are processed after this switch statement for clarity.
                previous_status = 0;
                switch (status) {
                    case 0xF0: // System Exclusive Message Begin
                    case 0xF7: // System Exclusive Message End
                        event_length = read_value(midi);
                        fprintf(
                            csv, "%lu, %lu, %s, %u, ", ntrack, timestamp,
                            (status == 0xF0) ? "System_exclusive" : "System_exclusive_packet", event_length
                        );
                        fprintn(csv, midi, event_length);
                        fputc('\n', csv);
                }
                break;

            default:
                fprintf(error_stream, "Unrecognised Status Byte: `%hhu`\n", status);
                break;
            }

            if (status != 0xFF) continue;

            // Meta events
            type = fgetc(midi);

            // Should be careful to protect against buffer overflows
            // and truncation of these records for large `event_length` values.
            event_length = read_value(midi);

            switch (type) {
            case MetaSequence:
                // `timestamp` should be 0 here.
                fprintf(csv, "%lu, 0, Sequence_number, %hu\n", ntrack, fgetc(midi) << 8 | fgetc(midi));
                fprintn(csv, midi, event_length);
                fputc('\n', csv);
                break;

            case MetaText:
                fprintf(csv, "%lu, %lu, Text_t, ", ntrack, timestamp);
                fprintn(csv, midi, event_length);
                fputc('\n', csv);
                break;

            case MetaCopyright:
                fprintf(csv, "%lu, %lu, Copyright_t, ", ntrack, timestamp);
                fprintn(csv, midi, event_length);
                fputc('\n', csv);
                break;

            case MetaTrackName:
                // TODO: Can save this info for verbosity sake.
                fprintf(csv, "%lu, %lu, Title_t, ", ntrack, timestamp);
                fprintn(csv, midi, event_length);
                fputc('\n', csv);
                break;

            case MetaInstrumentName:
                // TODO: Can save this info for verbosity sake.
                fprintf(csv, "%lu, %lu, Instrument_name_t, ", ntrack, timestamp);
                fprintn(csv, midi, event_length);
                fputc('\n', csv);
                break;

            case MetaLyrics:
                fprintf(csv, "%lu, %lu, Lyric_t, ", ntrack, timestamp);
                fprintn(csv, midi, event_length);
                fputc('\n', csv);
                break;

            case MetaMarker:
                fprintf(csv, "%lu, %lu, Marker_t, ", ntrack, timestamp);
                fprintn(csv, midi, event_length);
                fputc('\n', csv);
                break;

            case MetaCuePoint:
                fprintf(csv, "%lu, %lu, Cue_point_t, ", ntrack, timestamp);
                fprintn(csv, midi, event_length);
                fputc('\n', csv);
                break;

            case MetaChannelPrefix:
                buffer[0] = fgetc(midi);
                fprintf(csv, "%lu, %lu, Channel_prefix, %hhu\n", ntrack, timestamp, buffer[0]);
                break;

            case MetaEndOfTrack:
                end_of_track = 1;
                fprintf(csv, "%lu, %lu, End_track\n", ntrack, timestamp);
                break;

            case MetaSetTempo:
                // No of microseconds per MIDI quarter-note.
                // if (!tempo) // Following OLC's code.
                {
                    tempo = fgetc(midi) << 16 | fgetc(midi) << 8 | fgetc(midi);
                    // Quarter-notes or beats per minute
                    BPM = 60000000 / tempo;
                }
                fprintf(csv, "%lu, %lu, Tempo, %u\n", ntrack, timestamp, tempo);
                break;

            case MetaSMPTEOffset:
                // `timestamp` should be 0 here.
                // Specifies the SMPTE time code at which it should start playing.
                fprintf(
                    csv,
                    "%lu, 0, SMPTE_offset, %hhu, %hhu, %hhu, %hhu, %hhu",
                    ntrack, fgetc(midi), fgetc(midi), fgetc(midi), fgetc(midi), fgetc(midi));
                break;

            case MetaTimeSignature:
                buffer[0] = fgetc(midi);
                buffer[1] = fgetc(midi);
                buffer[2] = fgetc(midi);
                buffer[3] = fgetc(midi);
                fprintf(
                    csv, "%lu, %lu, Time_signature, %hhu, %hhu, %hhu, %hhu\n",
                    ntrack, timestamp, buffer[0], buffer[1], buffer[2], buffer[3]
                );
                break;

            case MetaKeySignature:
                // 0 for the key of C, a positive value for each sharp above C,
                // or a negative value for each flat below C, thus in the inclusive range −7 to 7.
                buffer[0] = fgetc(midi);
                // 1 if the key is minor else 0.
                buffer[1] = fgetc(midi);
                fprintf(
                    csv, "%lu, %lu, Key_signature, %hhd, \"%s\"\n",
                    ntrack, timestamp, buffer[0], (buffer[1]) ? "minor" : "major"
                );
                break;

            case MetaSequencerSpecific:
                // Used to store vendor-proprietary data in a MIDI file.
                fprintf(csv, "%lu, %lu, Sequencer_specific, %u, ", ntrack, timestamp, event_length);
                fprintn(csv, midi, event_length);
                fputc('\n', csv);
                break;

            default:
                fprintf(
                    csv, "%lu, %lu, Unknown_meta_event, %hhu, %u, ",
                    ntrack, timestamp, type, event_length
                );
                fprintn(csv, midi, event_length);
                fputc('\n', csv);
            }
        }
    }

    fprintf(csv, "0, 0, End_of_file\n");
    fprintf(error_stream, "BPM: %u\n", BPM);
    return 0;
}

int main(int argc, char **argv)
{
    FILE
    *midi = stdin,
    *csv = stdout;

    switch (argc) {
    case 3:
        csv = fopen(argv[2], "wb");
    case 2:
        midi = fopen(argv[1], "rb");
    }

    midi_to_csv(midi, csv);

    fclose(midi);
    fclose(csv);
    return 0;
}
