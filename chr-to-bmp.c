#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

const uint8_t bpp = 4; // amount of bits per pixel of the bmp file

const uint8_t palette[16] = {
    0xff, 0xff, 0xff, 0,  // white
    0xcc, 0x66, 0x66, 0,  //
    0xff, 0xcc, 0xcc, 0,  //
    0x00, 0x00, 0x00, 0}; // black

// merge chr layer rows (1bit per px) so we end up with a 16 bit row (2 bits per px)
int merge_chr_rows(uint8_t a, uint8_t b)
{
    int merged = 0;    // store merged bytes
    int mask = 1;      // current bit mask
    uint8_t x = 0;     // how much we are moving the current bit to the left
    while (mask < 129) // cover all 8 bits
    {
        // mask current bit and move to the left:
        // bits at index 0, end up at index 0 and 1,
        // bits at index 1, end up at index 2 and 3, etc
        merged |= ((a & mask) << x) | ((b & mask) << ++x);
        mask <<= 1; // next bit mask
    }
    return merged;
}

void order_chr(uint8_t *buffer, uint8_t *chr, int chrsize)
{
    // tables are ordered 1 above the other, with tiles for each layer next to each other
    // we are gonna order them like this: t0_layer0 t0_layer1 t1_layer0 t1_layer1
    for (int layer0_ndx = 0; layer0_ndx < chrsize; layer0_ndx += chrsize / 2)
    {
        int layer1_ndx = layer0_ndx + chrsize / 4;
        int table_end = layer0_ndx + chrsize / 2;

        // tile_ndx: index of the tile before ordering
        for (int tile_ndx = 0, i = 0 + layer0_ndx; i < table_end; ++i)
        {
            uint8_t byte_ndx = i % 8;
            int final_tile_ndx = tile_ndx / 2; // where the tile should go

            if (tile_ndx % 2) // even tiles go to tX_layer0
            {
                buffer[layer0_ndx + byte_ndx + final_tile_ndx * 8] = chr[i];
            }
            else // odd tiles go to tX_layer1
            {
                buffer[layer1_ndx + byte_ndx + final_tile_ndx * 8] = chr[i];
            }

            if (byte_ndx == 7)
            {
                ++tile_ndx;
            }
        }
    }
}

void merge_chr(uint8_t *buffer, uint8_t *ord_chr, int chrsize)
{
    const int tile_cols = sqrt(((chrsize / 4) / 8));
    const int bytes_per_row = tile_cols * 8;
    int row_ndx = 0;
    int col_ndx = 0;

    // merge first two layers (at 0 and chrsize * 1/4)
    // then merge the other two (at chrsize * 1/2 and chrsize * 3/4)
    for (int offset = 0; offset < chrsize; offset += chrsize / 2)
    {
        for (int i = 0; i < chrsize / 4; ++i)
        {
            uint8_t byte_ndx = i % 8; // current byte index in the current tile
            int c = merge_chr_rows(ord_chr[i + offset], ord_chr[i + offset + chrsize / 4]);

            // tilecols * 2 : each resulting col has double amount of bytes (layer0 + layer1)
            int ndx = (byte_ndx * tile_cols * 2) + col_ndx * 2 + row_ndx * bytes_per_row * 2;
            buffer[ndx] = c >> 8;
            buffer[ndx + 1] = c;

            if (i % bytes_per_row == bytes_per_row - 1)
            {
                ++row_ndx; // every 128 bytes we have a new tile row
            }

            if (i % 8 == 7)
            {
                col_ndx = (col_ndx + 1) % tile_cols;
            }
        }
    }
}

void decompress_chr(uint8_t *buffer, uint8_t *mer_chr, int chrsize)
{
    const uint8_t mask = 0b00000011; // keep only the first 2 bits
    uint8_t tmp = 0;
    for (int i = 0; i < chrsize; i++)
    {
        // transform the most important nibble into 1 byte
        tmp = (mer_chr[i] >> 6) & mask;
        tmp = (tmp << 4) | ((mer_chr[i] >> 4) & mask);
        buffer[i * 2] = tmp;

        // transform the least important nibble into 1 byte
        tmp = (mer_chr[i] >> 2) & mask;
        tmp = (tmp << 4) | (mer_chr[i] & mask);
        buffer[i * 2 + 1] = tmp;
    }
}

// get ready for some mind bending data transformation :)
void reverse(uint8_t *chr, int len)
{
    const int tile_cols = sqrt(((len / 4) / 8)); // 4 square layers, 8 byte tiles.
    // row size is doubled given that layer0 and layer1 tiles are stored next to each other
    const int bytes_per_row = tile_cols * 8 * 2;

    uint8_t tmp = 0;
    // traverse in a row by row basis swapping rows order
    for (int b_offset = 0, t_offset = len - bytes_per_row;
         b_offset < t_offset;
         b_offset += bytes_per_row, t_offset -= bytes_per_row)
    {
        // traverse tile by tile (8 byte chunks)
        for (int i = 0; i < bytes_per_row; i += 8)
        {
            // reverse byte order (tile columns)
            for (int j = 0, k = 7; j < 8; ++j, --k)
            {
                tmp = chr[i + k + b_offset];
                chr[i + k + b_offset] = chr[i + j + t_offset];
                chr[i + j + t_offset] = tmp;
            }
        }
    }
}

int main(int argc, char *argv[])
{
    if (argc < 3)
        return 1;

    // chr file in pixels
    int w = 128;
    int h = 256;

    FILE *input_file = fopen(argv[1], "rb");
    fseek(input_file, 0, SEEK_END);
    int filelen = ftell(input_file);
    int rawlen = filelen * 2; // input file has 2 bits per px, we need 4 bits in bmp (4bpp) format
    rewind(input_file);

    uint8_t *buffer = (uint8_t *)malloc(filelen);
    fread(buffer, filelen, 1, input_file);
    fclose(input_file);

    // reverse input file bytes so it ends up in the correct order for bmp (bottom to top px rows)
    reverse(buffer, filelen);

    // order each pattern table so we have:
    // 1st table layer0 at index 0 + 1st table layer1 at index filelen * 1/4
    // 2nd table layer0 at index filelen * 1/2 + 2nd table layer1 at index filelen * 3/4
    uint8_t *ordered = (uint8_t *)malloc(filelen);
    memset(ordered, 0, filelen);
    order_chr(ordered, buffer, filelen);

    // merge table layers
    uint8_t *merged = (uint8_t *)malloc(filelen);
    memset(merged, 0, filelen);
    merge_chr(merged, ordered, filelen);
    // we now have 4px per byte (2 bits per px), and rows ordered by scanline

    // decompress each byte (4px) into 2 bytes (1 byte per 2 px) for bmp 4bpp format
    uint8_t *decompressed = (uint8_t *)malloc(rawlen);
    memset(decompressed, 0, rawlen);
    decompress_chr(decompressed, merged, filelen);

    uint8_t fileheader[14] = {
        'B', 'M',
        0, 0, 0, 0, // file size in bytes
        0, 0,       // reserved 1
        0, 0,       // reserved 2
        0, 0, 0, 0  // byte offset where raw data is found
    };

    uint8_t infoheader[40] = {
        40, 0, 0, 0,  // size of this header
        0, 0, 0, 0,   // width
        0, 0, 0, 0,   // height
        1, 0,         // color planes: must be 1 by spec
        4, 0,         // bits per pixel
        0, 0, 0, 0,   // compression method: no compression
        0, 0, 0, 0,   // raw data size: can omit for no compression
        18, 11, 0, 0, // horizontal resolution in px/m
        18, 11, 0, 0, // vertical resolution in px/m
        4, 0, 0, 0,   // colors in palette
        0, 0, 0, 0    // important colors
    };

    // of course this is 70, I just wanna be really explicit with the reason :P
    int offset = sizeof(fileheader) + sizeof(infoheader) + sizeof(palette);
    int filesize = offset + rawlen;

    // convert all these values to little-endian:
    // file size
    fileheader[2] = (uint8_t)filesize;
    fileheader[3] = (uint8_t)(filesize >> 8);
    fileheader[4] = (uint8_t)(filesize >> 16);
    fileheader[5] = (uint8_t)(filesize >> 24);
    // raw data offset
    fileheader[10] = (uint8_t)offset;
    fileheader[11] = (uint8_t)(offset >> 8);
    fileheader[12] = (uint8_t)(offset >> 16);
    fileheader[13] = (uint8_t)(offset >> 24);
    // width
    infoheader[4] = (uint8_t)w;
    infoheader[5] = (uint8_t)(w >> 8);
    infoheader[6] = (uint8_t)(w >> 16);
    infoheader[7] = (uint8_t)(w >> 24);
    // height
    infoheader[8] = (uint8_t)h;
    infoheader[9] = (uint8_t)(h >> 8);
    infoheader[10] = (uint8_t)(h >> 16);
    infoheader[11] = (uint8_t)(h >> 24);

    FILE *output_file = fopen(argv[2], "wb");
    fwrite(fileheader, 1, sizeof(fileheader), output_file);
    fwrite(infoheader, 1, sizeof(infoheader), output_file);
    fwrite(palette, 1, 16, output_file);
    fwrite(decompressed, 1, rawlen, output_file);
    fclose(output_file);

    free(buffer);
    free(ordered);
    free(merged);
    free(decompressed);

    printf("%s generated: %d Bytes\n", argv[2], filesize);
}