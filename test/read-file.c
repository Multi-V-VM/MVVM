
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

FILE *fopen_test(const char *restrict filename, const char *restrict mode)
{
	FILE *f;
	int fd;
	int flags;

	/* Check for valid initial mode character */
	if (!strchr("rwa", *mode)) {
		return 0;
	}

	/* Compute the flags to pass to open() */
	flags = __fmodeflags(mode);

#ifdef __wasilibc_unmodified_upstream // WASI has no sys_open
	fd = sys_open(filename, flags, 0666);
#else
	// WASI libc ignores the mode parameter anyway, so skip the varargs.
	fd = __wasilibc_open_nomode(filename, flags);
    printf("\n fopen_test(fd,filename,flags) %d %s %d \n\n",fd,filename,flags);
#endif
	if (fd < 0) return 0;
#ifdef __wasilibc_unmodified_upstream // WASI has no syscall
	if (flags & O_CLOEXEC)
		__syscall(SYS_fcntl, fd, F_SETFD, FD_CLOEXEC);
#else
	/* Avoid __syscall, but also, FD_CLOEXEC is not supported in WASI. */
#endif

	f = __fdopen(fd, mode);
	if (f) return f;

#ifdef __wasilibc_unmodified_upstream // WASI has no syscall
	__syscall(SYS_close, fd);
#else
	close(fd);
#endif
	return 0;
}

int main() {
    FILE *file = fopen_test("./text.txt", "w");
    
    FILE *file1 = fopen_test("./text1.txt", "w");
    FILE *file2 = fopen_test("./text2.txt", "w");
    fseek(file,1,1);
    int c = 0;
    while (1){
        c++;
        if (c==100000){
            break;
        }
    }
    fprintf(file, "Successfully wrote to the file.");
    fclose(file);
    fclose(file1);
    fclose(file2);
}