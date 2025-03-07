#include "types.h"
#include "stat.h"
#include "user.h"
#include "fcntl.h"

int main() {
    int fd = open("testfile", O_RDWR | O_CREATE);
    if (fd < 0) {
        printf(1, "Error: Cannot open file\n");
        exit();
    }

    // Write some data to the file
    write(fd, "Hello, xv6!", 12);

    // Seek to the beginning of the file
    if (lseek(fd, 0, SEEK_SET) < 0) {
        printf(1, "SEEK_SET failed\n");
    } else {
        printf(1, "SEEK_SET successful\n");
    }

    // Seek forward by 5 bytes
    if (lseek(fd, 5, SEEK_CUR) < 0) {
        printf(1, "SEEK_CUR failed\n");
    } else {
        printf(1, "SEEK_CUR successful\n");
    }

    // Seek to the end
    if (lseek(fd, 0, SEEK_END) < 0) {
        printf(1, "SEEK_END failed\n");
    } else {
        printf(1, "SEEK_END successful\n");
    }

    close(fd);
    exit();
}