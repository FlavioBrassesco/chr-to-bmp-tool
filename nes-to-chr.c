#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int main(int argc, char *argv[])
{
    if (argc < 2)
        return 1;

    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    int filelen = ftell(f);
    rewind(f);

    uint8_t *buffer = (uint8_t *)malloc(filelen);
    fread(buffer, filelen, 1, f);
    fclose(f);

    if (buffer[0] != 0x4E && buffer[1] != 0x45 && buffer[2] != 0x53 && buffer[3] != 0x1A)
    {
        return 1;
    }
    
    printf("proper .nes file found \n");

    uint8_t prg_size_kb = buffer[4];
    uint8_t chr_size_kb = buffer[5];

    if (chr_size_kb == 0)
    {
        return 1;
    }

    printf("found %d compatible chr bank/s", chr_size_kb);

    uint8_t has_trainer = (buffer[6] & 0b00001000) >> 3;
    int header_size = 16;
    int trainer_size = 512 * has_trainer;
    int prg_size = prg_size_kb * 16384;
    int chr_size = chr_size_kb * 8192;

    int chr_banks_start = header_size + trainer_size + prg_size;

    for (int i = 0; i < chr_size_kb; i++)
    {
        // alloc memory for a chr bank and copy from buffer
        uint8_t *b = (uint8_t *)malloc(8192);
        memcpy(b, buffer + chr_banks_start + i * 8192, 8192);
        // prepare file name
        printf("%d\n", strlen(argv[1]));
        char *str = (char *)malloc(strlen(argv[1]) + 8);
        sprintf(str, "%s-%d.chr", argv[1], i);

        // write and free memory
        FILE *o = fopen(str, "wb");
        fwrite(b, 8192, 1, o);
        fclose(o);
        free(b);
    }
}