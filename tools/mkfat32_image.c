#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SECTOR_SIZE 512U
#define TOTAL_SECTORS 2048U
#define RESERVED_SECTORS 1U
#define FAT_COUNT 1U
#define FAT_SECTORS 16U
#define ROOT_CLUSTER 2U
#define FILE_FIRST_CLUSTER 3U
#define EDIT_CLUSTER_COUNT 8U
#define IMAGE_SIZE (TOTAL_SECTORS * SECTOR_SIZE)
#define FAT32_EOC 0x0fffffffU

static void put_le16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put_le32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static long file_size(FILE *file) {
    long size;

    if (fseek(file, 0, SEEK_END) != 0) {
        return -1;
    }

    size = ftell(file);
    if (size < 0 || fseek(file, 0, SEEK_SET) != 0) {
        return -1;
    }

    return size;
}

static void write_boot_sector(uint8_t *image) {
    uint8_t *boot = image;

    boot[0] = 0xeb;
    boot[1] = 0x58;
    boot[2] = 0x90;
    boot[3] = 'K';
    boot[4] = 'O';
    boot[5] = 'L';
    boot[6] = 'I';
    boot[7] = 'B';
    boot[8] = 'R';
    boot[9] = 'I';
    boot[10] = ' ';
    put_le16(&boot[11], SECTOR_SIZE);
    boot[13] = 1;
    put_le16(&boot[14], RESERVED_SECTORS);
    boot[16] = FAT_COUNT;
    put_le16(&boot[17], 0);
    put_le16(&boot[19], 0);
    boot[21] = 0xf8;
    put_le16(&boot[22], 0);
    put_le16(&boot[24], 1);
    put_le16(&boot[26], 1);
    put_le32(&boot[28], 0);
    put_le32(&boot[32], TOTAL_SECTORS);
    put_le32(&boot[36], FAT_SECTORS);
    put_le16(&boot[40], 0);
    put_le16(&boot[42], 0);
    put_le32(&boot[44], ROOT_CLUSTER);
    put_le16(&boot[48], 0);
    put_le16(&boot[50], 0);
    boot[64] = 0x80;
    boot[66] = 0x29;
    put_le32(&boot[67], 0x4b41524dU);
    boot[71] = 'K';
    boot[72] = 'O';
    boot[73] = 'L';
    boot[74] = 'I';
    boot[75] = 'B';
    boot[76] = 'R';
    boot[77] = 'I';
    boot[78] = 'A';
    boot[79] = 'R';
    boot[80] = 'M';
    boot[82] = 'F';
    boot[83] = 'A';
    boot[84] = 'T';
    boot[85] = '3';
    boot[86] = '2';
    boot[510] = 0x55;
    boot[511] = 0xaa;
}

static void write_cluster_chain(uint8_t *fat, uint32_t first_cluster,
                                uint32_t cluster_count) {
    for (uint32_t i = 0; i < cluster_count; i++) {
        uint32_t cluster = first_cluster + i;
        uint32_t next = i + 1U == cluster_count ? FAT32_EOC : cluster + 1U;

        put_le32(&fat[cluster * 4U], next);
    }
}

static void write_fat(uint8_t *image, uint32_t file_cluster_count,
                      uint32_t edit_first_cluster) {
    uint8_t *fat = &image[RESERVED_SECTORS * SECTOR_SIZE];

    put_le32(&fat[0], 0x0ffffff8U);
    put_le32(&fat[4], FAT32_EOC);
    put_le32(&fat[ROOT_CLUSTER * 4U], FAT32_EOC);
    write_cluster_chain(fat, FILE_FIRST_CLUSTER, file_cluster_count);
    write_cluster_chain(fat, edit_first_cluster, EDIT_CLUSTER_COUNT);
}

static void write_dir_entry(uint8_t *entry, const char name[11],
                            uint32_t first_cluster, uint32_t file_size_bytes) {
    for (uint32_t i = 0; i < 11U; i++) {
        entry[i] = (uint8_t)name[i];
    }

    entry[11] = 0x20;
    put_le16(&entry[20], 0);
    put_le16(&entry[26], first_cluster);
    put_le32(&entry[28], file_size_bytes);
}

static void write_root_entries(uint8_t *image, uint32_t file_size_bytes,
                               uint32_t edit_first_cluster) {
    uint32_t data_start = RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS;
    uint8_t *root = &image[data_start * SECTOR_SIZE];
    const char shell_name[11] = {
        'S', 'H', 'E', 'L', 'L', ' ', ' ', ' ', 'B', 'I', 'N',
    };
    const char edit_name[11] = {
        'E', 'D', 'I', 'T', ' ', ' ', ' ', ' ', 'T', 'X', 'T',
    };
    static const char edit_text[] = "KolibriARM editable FAT32 file\n";

    write_dir_entry(root, shell_name, FILE_FIRST_CLUSTER,
                    file_size_bytes);
    write_dir_entry(root + 32U, edit_name, edit_first_cluster,
                    sizeof(edit_text) - 1U);
}

static int copy_payload(uint8_t *image, FILE *input, uint32_t file_size_bytes) {
    uint32_t data_start = RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS;
    uint32_t file_lba = data_start + (FILE_FIRST_CLUSTER - ROOT_CLUSTER);
    uint8_t *dest = &image[file_lba * SECTOR_SIZE];

    return fread(dest, 1, file_size_bytes, input) == file_size_bytes ? 0 : -1;
}

static void write_edit_payload(uint8_t *image, uint32_t edit_first_cluster) {
    uint32_t data_start = RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS;
    uint32_t file_lba = data_start + (edit_first_cluster - ROOT_CLUSTER);
    uint8_t *dest = &image[file_lba * SECTOR_SIZE];
    static const char edit_text[] = "KolibriARM editable FAT32 file\n";

    for (uint32_t i = 0; i + 1U < sizeof(edit_text); i++) {
        dest[i] = (uint8_t)edit_text[i];
    }
}

int main(int argc, char **argv) {
    FILE *input;
    FILE *output;
    uint8_t *image;
    long input_size;
    uint32_t cluster_count;
    uint32_t edit_first_cluster;
    uint32_t max_payload;

    if (argc != 3) {
        fprintf(stderr, "usage: %s output.img input.bin\n", argv[0]);
        return 1;
    }

    input = fopen(argv[2], "rb");
    if (input == NULL) {
        perror(argv[2]);
        return 1;
    }

    input_size = file_size(input);
    max_payload = (TOTAL_SECTORS - (RESERVED_SECTORS + FAT_COUNT * FAT_SECTORS) -
                   (FILE_FIRST_CLUSTER - ROOT_CLUSTER) -
                   EDIT_CLUSTER_COUNT) * SECTOR_SIZE;
    if (input_size <= 0 || (uint32_t)input_size > max_payload) {
        fprintf(stderr, "input size is invalid for tiny FAT32 image\n");
        fclose(input);
        return 1;
    }

    image = (uint8_t *)calloc(1, IMAGE_SIZE);
    if (image == NULL) {
        fclose(input);
        return 1;
    }

    cluster_count = ((uint32_t)input_size + SECTOR_SIZE - 1U) / SECTOR_SIZE;
    edit_first_cluster = FILE_FIRST_CLUSTER + cluster_count;
    write_boot_sector(image);
    write_fat(image, cluster_count, edit_first_cluster);
    write_root_entries(image, (uint32_t)input_size, edit_first_cluster);
    if (copy_payload(image, input, (uint32_t)input_size) != 0) {
        fprintf(stderr, "failed to copy input payload\n");
        free(image);
        fclose(input);
        return 1;
    }
    write_edit_payload(image, edit_first_cluster);
    fclose(input);

    output = fopen(argv[1], "wb");
    if (output == NULL) {
        perror(argv[1]);
        free(image);
        return 1;
    }

    if (fwrite(image, 1, IMAGE_SIZE, output) != IMAGE_SIZE) {
        fprintf(stderr, "failed to write image\n");
        fclose(output);
        free(image);
        return 1;
    }

    fclose(output);
    free(image);
    return 0;
}
