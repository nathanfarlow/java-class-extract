#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <endian.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/limits.h>
#include <errno.h>

// See https://docs.oracle.com/javase/specs/jvms/se16/jvms16.pdf
// for Java class file format

// If the class is any larger than this, it's probably corrupted.
// Increase if you plan on extracting class files larger than 10MB
#define MAX_CLASS_SIZE 10 * 1024 * 1024

typedef uint8_t u1;
typedef uint16_t u2;
typedef uint32_t u4;

u1 _read_u1(const u1 **data) { return *(*data)++; }

u2 _read_u2(const u1 **data) {
    u2 val = be16toh(*(const u2*)(*data));
    *data += 2;
    return val;
}

u4 _read_u4(const u1 **data) {
    u4 val = be32toh(*(const u4*)(*data));
    *data += 4;
    return val;
}

#define _read(bytes, val, data, end)            \
    do {                                        \
        if (*(data) > (end) - bytes) return -1; \
        val = _read_u##bytes(data);             \
    } while (0)

#define read_u1(val, data, end) _read(1, val, data, end)
#define read_u2(val, data, end) _read(2, val, data, end) 
#define read_u4(val, data, end) _read(4, val, data, end)

ssize_t read_cp_info(const u1 **data, const u1 *end, u1 tag) {
    const u1 *start = *data;

    if (tag == 1) {
        // CONSTANT_Utf8
        u2 length;
        read_u2(length, data, end);
        *data += length;
    } else {
        // All other constants
        const u1 sizes[] = {0, 0, 0, 4, 4, 8, 8, 2, 2, 4, 4, 4, 4, 0, 0, 3, 2, 4, 4, 2, 2};
        if (tag >= sizeof(sizes) || !sizes[tag]) return -1;
        *data += sizes[tag];
    }

    return *data - start;
}

ssize_t read_attribute_info(const u1 **data, const u1 *end) {
    const u1 *start = *data;

    // attribute_name_index
    *data += 2;

    u4 attribute_length;
    read_u4(attribute_length, data, end);
    *data += attribute_length;

    return *data - start;
}

ssize_t read_field_info(const u1 **data, const u1 *end) {
    const u1 *start = *data;

    // access_flags, name_index, descriptor_index
    *data += 6;

    u2 attributes_count;
    read_u2(attributes_count, data, end);
    for (u2 i = 0; i < attributes_count; i++) {
        if (read_attribute_info(data, end) < 0) return -1;
    }

    return *data - start;
}

ssize_t read_class(const u1 *data, const u1 *end) {
    const u1 *start = data;

    // magic, minor_version, major_version
    data += 8;

    u2 constant_pool_count;
    read_u2(constant_pool_count, &data, end);
    if (constant_pool_count == 0) return -1;
    for (u2 i = 0; i < constant_pool_count - 1; i++) {
        u1 tag;
        read_u1(tag, &data, end);
        if (read_cp_info(&data, end, tag) < 0) return -1;

        // Long and Double constants occupy 2 constant pool entries
        i += tag == 5 || tag == 6;
    }

    // access_flags, this_class, super_class
    data += 6;

    u2 interfaces_count;
    read_u2(interfaces_count, &data, end);
    data += 2 * interfaces_count;

    // field_info and method_info have the same schema
    for (u1 i = 0; i < 2; i++) {
        u2 field_or_method_count;
        read_u2(field_or_method_count, &data, end);
        for (u2 j = 0; j < field_or_method_count; j++) {
            if (read_field_info(&data, end) < 0) return -1;
        }
    }

    u2 attributes_count;
    read_u2(attributes_count, &data, end);
    for (u2 i = 0; i < attributes_count; i++) {
        if (read_attribute_info(&data, end) < 0) return -1;
    }

    return data - start; 
}

ssize_t find_next_class(const u1 **data, const u1 *end) {
    const u1 *start = *data;

    while (1) {
        u4 magic;
        read_u4(magic, data, end);

        if (magic == 0xCAFEBABE) {
            *data -= 4;
            return *data - start;
        }

        *data -= 3;
    }
}

void write_file(const u1 *data, size_t len, const char *fname) {
    FILE *file = fopen(fname, "wb"); 

    if (!file) {
        perror("Can't save class, does the directory exist and is it writeable? ");
        return;
    }

    fwrite(data, len, 1, file);
    fclose(file);
}

void dump(const u1 *data, size_t len, const char *out_dir) {
    const u1 *start = data, *end = data + len;

    size_t num_found = 0;
    while (find_next_class(&data, end) >= 0) {
        ssize_t class_size = read_class(data, end);

        if (class_size >= 0 && class_size <= MAX_CLASS_SIZE) {
            const char fname[PATH_MAX];
            snprintf((char*)fname, sizeof(fname), "%s/%lu.class", out_dir, num_found++);
            printf("Found %ld byte class at offset 0x%lx. Saving to %s\n", class_size, data - start, fname);
            write_file(data, class_size, fname);
        }

        data++;
    }
}

int main(int argc, char **argv) {

    int fd;
    u1 *data;
    struct stat sbuf;
    const char *error = "Failed to open file";
    
    if (argc != 3) {
        fprintf(stderr, "Usage: dumpclass [file] [output directory]\n");
        exit(1);
    }

    if ((fd = open(argv[1], O_RDONLY)) == -1) {
        perror(error);
        exit(1);
    }

    if (stat(argv[1], &sbuf) == -1) {
        perror(error);
        exit(1);
    }

    data = mmap(0, sbuf.st_size, PROT_READ, MAP_SHARED, fd, 0);

    if (data == (void*)-1) {
        perror(error);
        exit(1);
    }

    dump(data, sbuf.st_size, argv[2]);

    munmap(data, sbuf.st_size);

    return 0;
}
