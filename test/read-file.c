
// #include <cstdio>
#include <stdbool.h>
#include <stdio.h>
#include <sys/fcntl.h>
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
   // fseek(file,1,1);

   /** Test fread*/
   FILE *file3 = fopen_test("./test.txt", "a");
   const char* line1 = "This is line 1\n";
   const char* line2 = "This is line 2\n";
   size_t len1 = strlen(line1);
   size_t len2 = strlen(line2);

   fwrite(line1, sizeof(char), len1, file3);
   //ntwritten
   // checkpoint: lib_wasi_wrapper fwrite system record 
   

    // volatile int c = 0;
    // for( int i =0;i<10000;i++){
    //     c++;
    // }

	fwrite(line2, sizeof(char), len2, file3);
	
    fprintf(file, "Successfully wrote to the file.");
    fclose(file);
    fclose(file1);
    fclose(file2);
	fclose(file3);
	__wasi_fd_renumber(3,10);
}