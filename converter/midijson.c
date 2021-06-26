/*
Resources:
http://www.music.mcgill.ca/~ich/classes/mumt306/StandardMIDIfileformat.html
https://www.fourmilab.ch/webtools/midijson/
https://www.cs.cmu.edu/~music/cmsip/readings/davids-midi-spec.htm
https://github.com/OneLoneCoder/olcPixelGameEngine/blob/master/Videos/OneLoneCoder_PGE_MIDI.cpp
http://www.gweep.net/~prefect/eng/reference/protocol/midispec.html
https://www.eecs.umich.edu/courses/eecs373/Lec/StudentF18/MIDI%20Presentation.pdf

Usage:
gcc main.c -o main -Wall
./main music.mid music.json
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

uint8_t midi_to_json(FILE *midi, FILE *json)
{
    uint32_t timestamp = 0;

    uint32_t
    delta_time,
    chunk_length,
    event_length,
    tempo = 0,
    BPM = 0;

    uint8_t
    previous_status,
    status,
    channel,
    end_of_track,
    type,
    first_track = 1,
    first_event,
    first_data;

    char
    buffer[64],
    chunk_id[4];

    FILE *error_stream = stderr;

    struct HeaderChunk header = create_header_chunk(midi);

    if (strncmp(header.chunk_id, "MThd", 4) || (header.format == 0 && header.track_chunks != 1)) {
        return InvalidHeaderChunk;
    }

    fprintf(
        json, "{\"format\":%hu,\"time_division\":%hu,\"track_count\":%hu,\"tracks\":[",
        header.format, header.time_division, header.track_chunks
    );

    for (uint32_t ntrack = 1; ntrack <= header.track_chunks; ++ntrack) {
        end_of_track = 0;
        timestamp = 0;

        // Track chunk length in bytes. Skip this number of bytes to get to next track.
        fread(chunk_id, 1, 4, midi);
        fread(&chunk_length, 4, 1, midi);
        chunk_length = swap32(chunk_length);

        if (strncmp(chunk_id, "MTrk", 4)) {
            return InvalidTrackChunk;
        }

        if (!first_track)
            fputc(',', json);
        fprintf(json, "{\"events\":[");
        
        first_track = 0;
        first_event = 1;

        while (!feof(midi) && !end_of_track) {
            delta_time = read_value(midi);
            timestamp += delta_time;
            status = fgetc(midi);

            if (status < 0x80) {
                status = previous_status;
                fseek(midi, -1, SEEK_CUR);
            }

            previous_status = status;
            channel = status & 0x0F;

            if (!first_event)
                fputc(',', json);
            fprintf(
                json, "{\"timestamp\":%u,\"delta_time\":%u,\"type\":%hhu,",
                timestamp, delta_time, (status & 0xF0 ^ 0xF0) ? status & 0xF0 : status
            );

            event_length = 1;
            first_event = 0;
            first_data = 1;

            switch (status & 0xF0) {
            case EventNoteOn:
            case EventNoteOff:
            case EventKeyPressure:
            case EventControllerChange:
            case EventPitchBend:
                event_length = 2;
            case EventProgramChange:
            case EventChannelPressure:
                fprintf(json, "\"channel\":%hhu,\"length\":%u,\"data\":", channel, event_length);
                fputc('[', json);
                while (event_length--) {
                    if (!first_data)
                        fputc(',', json);
                    fprintf(json, "%hhu", fgetc(midi));
                    first_data = 0;
                }
                fputc(']', json);
                break;

            case EventSystemExclusive:
                previous_status = 0;
                switch (status) {
                case 0xF0: // System Exclusive Message Begin
                case 0xF7: // System Exclusive Message End
                    event_length = read_value(midi);
                    fprintf(json, "\"channel\":%hhu,\"length\":%u,\"data\":", channel, event_length);
                    fprintn(json, midi, event_length);
                    break;

                case 0xFF:
                    type = fgetc(midi);
                    event_length = read_value(midi);
                    fprintf(json, "\"metatype\":%hhu,\"length\":%u,\"data\":", type, event_length);

                    switch (type) {
                    case MetaText:
                    case MetaCopyright:
                    case MetaTrackName:
                    case MetaInstrumentName:
                    case MetaLyrics:
                    case MetaMarker:
                    case MetaCuePoint:
                    case MetaSequencerSpecific:
                        fprintn(json, midi, event_length);
                        break;

                    case MetaEndOfTrack:
                        end_of_track = 1;
                    case MetaSequence:
                    case MetaChannelPrefix:
                    case MetaSetTempo:
                    case MetaSMPTEOffset:
                    case MetaTimeSignature:
                    case MetaKeySignature:
                    default:
                        fputc('[', json);
                        while (event_length--) {
                            fprintf(json, "%s%hhu", (first_data) ? "" : ",", fgetc(midi));
                            first_data = 0;
                        }
                        fputc(']', json);
                    }
                }
                break;

            default:
                fprintf(error_stream, "Unrecognised Status Byte: `%hhu`\n", status);
            }
            fputc('}', json);
        }
        fputs("]}", json);
    }
    fputs("]}", json);

    fprintf(error_stream, "BPM: %u\n", BPM);
    return 0;
}

int main(int argc, char **argv)
{
    FILE
    *midi = stdin,
    *json = stdout;

    switch (argc) {
    case 3:
        json = fopen(argv[2], "wb");
    case 2:
        midi = fopen(argv[1], "rb");
    }

    midi_to_json(midi, json);

    fclose(midi);
    fclose(json);
    return 0;
}