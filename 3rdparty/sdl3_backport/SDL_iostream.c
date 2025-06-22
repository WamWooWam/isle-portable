/*
  Simple DirectMedia Layer
  Copyright (C) 1997-2025 Sam Lantinga <slouken@libsdl.org>

  This software is provided 'as-is', without any express or implied
  warranty.  In no event will the authors be held liable for any damages
  arising from the use of this software.

  Permission is granted to anyone to use this software for any purpose,
  including commercial applications, and to alter it and redistribute it
  freely, subject to the following restrictions:

  1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.
  2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.
  3. This notice may not be removed or altered from any source distribution.
*/
#include "SDL2/SDL.h"

#if defined(SDL_PLATFORM_WINDOWS)
#include "../core/windows/SDL_windows.h"
#else
#include <unistd.h>
#endif

#ifdef HAVE_STDIO_H
#include <stdio.h>
#include <errno.h>
#include <sys/stat.h>
#endif
#ifdef HAVE_LIMITS_H
#include <limits.h>
#endif

#ifdef SDL_PLATFORM_APPLE
#include <fcntl.h>
#endif

#include "SDL_iostream_c.h"



/* This file provides a general interface for SDL to read and write
   data sources.  It can easily be extended to files, memory, etc.
*/

typedef struct {
	void* memoryPtr;
	int memorySize;
	void* dynamicMemPtr;
	void* stdioFilePtr;
	int fdNumber;
	int chunkSize;
} SDL_PropertiesID;

struct SDL_IOStream
{
    SDL_IOStreamInterface iface;
    void *userdata;
    SDL_IOStatus status;
	SDL_PropertiesID props;
};

static SDL_PropertiesID* SDL_GetIOProperties(SDL_IOStream *context) {
	return &context->props;
}

#ifdef SDL_PLATFORM_3DS
#include "n3ds/SDL_iostreamromfs.h"
#endif // SDL_PLATFORM_3DS

#ifdef SDL_PLATFORM_ANDROID
#include <unistd.h>
#include "../core/android/SDL_android.h"
#endif

#if !defined(SDL_PLATFORM_WINDOWS)

// Functions to read/write file descriptors. Not used for windows.

typedef struct IOStreamFDData
{
    int fd;
    SDL_bool autoclose;
    SDL_bool regular_file;
} IOStreamFDData;

static int SDL_fdatasync(int fd)
{
    int result = 0;

#if defined(SDL_PLATFORM_APPLE)  // Apple doesn't have fdatasync (rather, the symbol exists as an incompatible system call).
    result = fcntl(fd, F_FULLFSYNC);
#elif defined(SDL_PLATFORM_HAIKU)
    result = fsync(fd);
#elif defined(HAVE_FDATASYNC)
    result = fdatasync(fd);
#endif
    return result;
}

static Sint64 SDLCALL fd_seek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
    IOStreamFDData *iodata = (IOStreamFDData *) userdata;
    int fdwhence;

    switch (whence) {
    case SDL_IO_SEEK_SET:
        fdwhence = SEEK_SET;
        break;
    case SDL_IO_SEEK_CUR:
        fdwhence = SEEK_CUR;
        break;
    case SDL_IO_SEEK_END:
        fdwhence = SEEK_END;
        break;
    default:
        SDL_SetError("Unknown value for 'whence'");
        return -1;
    }

    off_t result = lseek(iodata->fd, (off_t)offset, fdwhence);
    if (result < 0) {
        SDL_SetError("Couldn't get stream offset: %s", strerror(errno));
    }
    return result;
}

static size_t SDLCALL fd_read(void *userdata, void *ptr, size_t size, SDL_IOStatus *status)
{
    IOStreamFDData *iodata = (IOStreamFDData *) userdata;
    ssize_t bytes;
    do {
        bytes = read(iodata->fd, ptr, size);
    } while (bytes < 0 && errno == EINTR);

    if (bytes < 0) {
        if (errno == EAGAIN) {
            *status = SDL_IO_STATUS_NOT_READY;
        } else {
            SDL_SetError("Error reading from datastream: %s", strerror(errno));
        }
        bytes = 0;
    }
    return (size_t)bytes;
}

static size_t SDLCALL fd_write(void *userdata, const void *ptr, size_t size, SDL_IOStatus *status)
{
    IOStreamFDData *iodata = (IOStreamFDData *) userdata;
    ssize_t bytes;
    do {
        bytes = write(iodata->fd, ptr, size);
    } while (bytes < 0 && errno == EINTR);

    if (bytes < 0) {
        if (errno == EAGAIN) {
            *status = SDL_IO_STATUS_NOT_READY;
        } else {
            SDL_SetError("Error writing to datastream: %s", strerror(errno));
        }
        bytes = 0;
    }
    return (size_t)bytes;
}

static SDL_bool SDLCALL fd_flush(void *userdata, SDL_IOStatus *status)
{
    IOStreamFDData *iodata = (IOStreamFDData *) userdata;
    int result;
    do {
        result = SDL_fdatasync(iodata->fd);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        return SDL_SetError("Error flushing datastream: %s", strerror(errno));
    }
    return SDL_TRUE;
}

static SDL_bool SDLCALL fd_close(void *userdata)
{
    IOStreamFDData *iodata = (IOStreamFDData *) userdata;
    SDL_bool status = SDL_TRUE;
    if (iodata->autoclose) {
        if (close(iodata->fd) < 0) {
            status = SDL_SetError("Error closing datastream: %s", strerror(errno));
        }
    }
    SDL_free(iodata);
    return status;
}

SDL_IOStream *SDL_IOFromFD(int fd, SDL_bool autoclose)
{
    IOStreamFDData *iodata = (IOStreamFDData *) SDL_calloc(1, sizeof (*iodata));
    if (!iodata) {
        if (autoclose) {
           close(fd);
        }
        return NULL;
    }

    SDL_IOStreamInterface iface;
    //SDL_INIT_INTERFACE(&iface);
    memset(&iface, 0, sizeof(SDL_IOStreamInterface));
	iface.version = sizeof(SDL_IOStreamInterface);
	
	// There's no fd_size because SDL_GetIOSize emulates it the same way we'd do it for fd anyhow.
    iface.seek = fd_seek;
    iface.read = fd_read;
    iface.write = fd_write;
    iface.flush = fd_flush;
    iface.close = fd_close;

    iodata->fd = fd;
    iodata->autoclose = autoclose;

    struct stat st;
    iodata->regular_file = ((fstat(fd, &st) == 0) && S_ISREG(st.st_mode));

    SDL_IOStream *iostr = SDL_OpenIO(&iface, iodata);
    if (!iostr) {
        iface.close(iodata);
    } else {
        SDL_PropertiesID* props = SDL_GetIOProperties(iostr);
        if (props) {
            //SDL_SetNumberProperty(props, SDL_PROP_IOSTREAM_FILE_DESCRIPTOR_NUMBER, fd);
			props->fdNumber = fd;
		}
    }

    return iostr;
}
#endif // !defined(SDL_PLATFORM_WINDOWS)

#if defined(HAVE_STDIO_H) && !defined(SDL_PLATFORM_WINDOWS)

// Functions to read/write stdio file pointers. Not used for windows.

typedef struct IOStreamStdioData
{
    FILE *fp;
    SDL_bool autoclose;
    SDL_bool regular_file;
} IOStreamStdioData;

#ifdef HAVE_FOPEN64
#define fopen fopen64
#endif
#ifdef HAVE_FSEEKO64
#define fseek_off_t off64_t
#define fseek       fseeko64
#define ftell       ftello64
#elif defined(HAVE_FSEEKO)
#if defined(OFF_MIN) && defined(OFF_MAX)
#define FSEEK_OFF_MIN OFF_MIN
#define FSEEK_OFF_MAX OFF_MAX
#elif defined(HAVE_LIMITS_H)
/* POSIX doesn't specify the minimum and maximum macros for off_t so
 * we have to improvise and dance around implementation-defined
 * behavior. This may fail if the off_t type has padding bits or
 * is not a two's-complement representation. The compilers will detect
 * and eliminate the dead code if off_t has 64 bits.
 */
#define FSEEK_OFF_MAX (((((off_t)1 << (sizeof(off_t) * CHAR_BIT - 2)) - 1) << 1) + 1)
#define FSEEK_OFF_MIN (-(FSEEK_OFF_MAX)-1)
#endif
#define fseek_off_t off_t
#define fseek       fseeko
#define ftell       ftello
#elif defined(HAVE__FSEEKI64)
#define fseek_off_t __int64
#define fseek       _fseeki64
#define ftell       _ftelli64
#else
#ifdef HAVE_LIMITS_H
#define FSEEK_OFF_MIN LONG_MIN
#define FSEEK_OFF_MAX LONG_MAX
#endif
#define fseek_off_t long
#endif

static Sint64 SDLCALL stdio_seek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
    IOStreamStdioData *iodata = (IOStreamStdioData *) userdata;
    int stdiowhence;

    switch (whence) {
    case SDL_IO_SEEK_SET:
        stdiowhence = SEEK_SET;
        break;
    case SDL_IO_SEEK_CUR:
        stdiowhence = SEEK_CUR;
        break;
    case SDL_IO_SEEK_END:
        stdiowhence = SEEK_END;
        break;
    default:
        SDL_SetError("Unknown value for 'whence'");
        return -1;
    }

#if defined(FSEEK_OFF_MIN) && defined(FSEEK_OFF_MAX)
    if (offset < (Sint64)(FSEEK_OFF_MIN) || offset > (Sint64)(FSEEK_OFF_MAX)) {
        SDL_SetError("Seek offset out of range");
        return -1;
    }
#endif

    // don't make a possibly-costly API call for the noop seek from SDL_TellIO
    const SDL_bool is_noop = (whence == SDL_IO_SEEK_CUR) && (offset == 0);

    if (is_noop || fseek(iodata->fp, (fseek_off_t)offset, stdiowhence) == 0) {
        const Sint64 pos = ftell(iodata->fp);
        if (pos < 0) {
            SDL_SetError("Couldn't get stream offset: %s", strerror(errno));
            return -1;
        }
        return pos;
    }
    SDL_SetError("Error seeking in datastream: %s", strerror(errno));
    return -1;
}

static size_t SDLCALL stdio_read(void *userdata, void *ptr, size_t size, SDL_IOStatus *status)
{
    IOStreamStdioData *iodata = (IOStreamStdioData *) userdata;
    const size_t bytes = fread(ptr, 1, size, iodata->fp);
    if (bytes == 0 && ferror(iodata->fp)) {
        if (errno == EAGAIN) {
            *status = SDL_IO_STATUS_NOT_READY;
            clearerr(iodata->fp);
        } else {
            SDL_SetError("Error reading from datastream: %s", strerror(errno));
        }
    }
    return bytes;
}

static size_t SDLCALL stdio_write(void *userdata, const void *ptr, size_t size, SDL_IOStatus *status)
{
    IOStreamStdioData *iodata = (IOStreamStdioData *) userdata;
    const size_t bytes = fwrite(ptr, 1, size, iodata->fp);
    if (bytes == 0 && ferror(iodata->fp)) {
        if (errno == EAGAIN) {
            *status = SDL_IO_STATUS_NOT_READY;
            clearerr(iodata->fp);
        } else {
            SDL_SetError("Error writing to datastream: %s", strerror(errno));
        }
    }
    return bytes;
}

static SDL_bool SDLCALL stdio_flush(void *userdata, SDL_IOStatus *status)
{
    IOStreamStdioData *iodata = (IOStreamStdioData *) userdata;
    if (fflush(iodata->fp) != 0) {
        if (errno == EAGAIN) {
            *status = SDL_IO_STATUS_NOT_READY;
            return SDL_FALSE;
        } else {
            return SDL_SetError("Error flushing datastream: %s", strerror(errno));
        }
    }

    int result;
    int fd = fileno(iodata->fp);
    do {
        result = SDL_fdatasync(fd);
    } while (result < 0 && errno == EINTR);

    if (result < 0) {
        return SDL_SetError("Error flushing datastream: %s", strerror(errno));
    }
    return SDL_TRUE;
}

static SDL_bool SDLCALL stdio_close(void *userdata)
{
    IOStreamStdioData *iodata = (IOStreamStdioData *) userdata;
    SDL_bool status = SDL_TRUE;
    if (iodata->autoclose) {
        if (fclose(iodata->fp) != 0) {
            status = SDL_SetError("Error closing datastream: %s", strerror(errno));
        }
    }
    SDL_free(iodata);
    return status;
}

SDL_IOStream *SDL_IOFromFP(FILE *fp, SDL_bool autoclose)
{
    IOStreamStdioData *iodata = (IOStreamStdioData *) SDL_calloc(1, sizeof (*iodata));
    if (!iodata) {
        if (autoclose) {
           fclose(fp);
        }
        return NULL;
    }

    SDL_IOStreamInterface iface;
    memset(&iface, 0, sizeof(SDL_IOStreamInterface));
	iface.version = sizeof(SDL_IOStreamInterface);
	// There's no stdio_size because SDL_GetIOSize emulates it the same way we'd do it for stdio anyhow.
    iface.seek = stdio_seek;
    iface.read = stdio_read;
    iface.write = stdio_write;
    iface.flush = stdio_flush;
    iface.close = stdio_close;

    iodata->fp = fp;
    iodata->autoclose = autoclose;

    struct stat st;
    iodata->regular_file = ((fstat(fileno(fp), &st) == 0) && S_ISREG(st.st_mode));

    SDL_IOStream *iostr = SDL_OpenIO(&iface, iodata);
    if (!iostr) {
        iface.close(iodata);
    } else {
        SDL_PropertiesID* props = SDL_GetIOProperties(iostr);
        if (props) {
            //SDL_SetPointerProperty(props, SDL_PROP_IOSTREAM_STDIO_FILE_POINTER, fp);
            //SDL_SetNumberProperty(props, SDL_PROP_IOSTREAM_FILE_DESCRIPTOR_NUMBER, fileno(fp));
			
			props->stdioFilePtr = fp;
			props->fdNumber = fileno(fp);
        }
    }

    return iostr;
}
#endif // !HAVE_STDIO_H && !defined(SDL_PLATFORM_WINDOWS)

// Functions to read/write memory pointers

typedef struct IOStreamMemData
{
    Uint8 *base;
    Uint8 *here;
    Uint8 *stop;
} IOStreamMemData;

static Sint64 SDLCALL mem_size(void *userdata)
{
    const IOStreamMemData *iodata = (IOStreamMemData *) userdata;
    return (iodata->stop - iodata->base);
}

static Sint64 SDLCALL mem_seek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
    IOStreamMemData *iodata = (IOStreamMemData *) userdata;
    Uint8 *newpos;

    switch (whence) {
    case SDL_IO_SEEK_SET:
        newpos = iodata->base + offset;
        break;
    case SDL_IO_SEEK_CUR:
        newpos = iodata->here + offset;
        break;
    case SDL_IO_SEEK_END:
        newpos = iodata->stop + offset;
        break;
    default:
        SDL_SetError("Unknown value for 'whence'");
        return -1;
    }
    if (newpos < iodata->base) {
        newpos = iodata->base;
    }
    if (newpos > iodata->stop) {
        newpos = iodata->stop;
    }
    iodata->here = newpos;
    return (Sint64)(iodata->here - iodata->base);
}

static size_t mem_io(void *userdata, void *dst, const void *src, size_t size)
{
    IOStreamMemData *iodata = (IOStreamMemData *) userdata;
    const size_t mem_available = (iodata->stop - iodata->here);
    if (size > mem_available) {
        size = mem_available;
    }
    SDL_memcpy(dst, src, size);
    iodata->here += size;
    return size;
}

static size_t SDLCALL mem_read(void *userdata, void *ptr, size_t size, SDL_IOStatus *status)
{
    IOStreamMemData *iodata = (IOStreamMemData *) userdata;
    return mem_io(userdata, ptr, iodata->here, size);
}

static size_t SDLCALL mem_write(void *userdata, const void *ptr, size_t size, SDL_IOStatus *status)
{
    IOStreamMemData *iodata = (IOStreamMemData *) userdata;
    return mem_io(userdata, iodata->here, ptr, size);
}

static SDL_bool SDLCALL mem_close(void *userdata)
{
    SDL_free(userdata);
    return SDL_TRUE;
}

// Functions to create SDL_IOStream structures from various data sources

#if defined(HAVE_STDIO_H) && !defined(SDL_PLATFORM_WINDOWS)
static SDL_bool IsRegularFileOrPipe(FILE *f)
{
#ifndef SDL_PLATFORM_EMSCRIPTEN
    struct stat st;
    if (fstat(fileno(f), &st) < 0 || !(S_ISREG(st.st_mode) || S_ISFIFO(st.st_mode))) {
        return SDL_FALSE;
    }
#endif // !SDL_PLATFORM_EMSCRIPTEN

    return SDL_TRUE;
}
#endif

SDL_IOStream *SDL_IOFromFile(const char *file, const char *mode)
{
    SDL_IOStream *iostr = NULL;

    if (!file || !*file) {
        SDL_InvalidParamError("file");
        return NULL;
    }
    if (!mode || !*mode) {
        SDL_InvalidParamError("mode");
        return NULL;
    }

#ifdef SDL_PLATFORM_ANDROID
#ifdef HAVE_STDIO_H
    // Try to open the file on the filesystem first
    if (*file == '/') {
        FILE *fp = fopen(file, mode);
        if (fp) {
            if (!IsRegularFileOrPipe(fp)) {
                fclose(fp);
                SDL_SetError("%s is not a regular file or pipe", file);
                return NULL;
            }
            return SDL_IOFromFP(fp, SDL_TRUE);
        }
    } else if (SDL_strncmp(file, "content://", 10) == 0) {
        // Try opening content:// URI
        int fd = Android_JNI_OpenFileDescriptor(file, mode);
        if (fd == -1) {
            // SDL error is already set.
            return NULL;
        }

        FILE *fp = fdopen(fd, mode);
        if (!fp) {
            close(fd);
            SDL_SetError("Unable to open file descriptor (%d) from URI %s: %s", fd, file, strerror(errno));
            return NULL;
        }

        return SDL_IOFromFP(fp, SDL_TRUE);
    } else {
        // Try opening it from internal storage if it's a relative path
        char *path = NULL;
        SDL_asprintf(&path, "%s/%s", SDL_GetAndroidInternalStoragePath(), file);
        if (path) {
            FILE *fp = fopen(path, mode);
            SDL_free(path);
            if (fp) {
                if (!IsRegularFileOrPipe(fp)) {
                    fclose(fp);
                    SDL_SetError("%s is not a regular file or pipe", path);
                    return NULL;
                }
                return SDL_IOFromFP(fp, SDL_TRUE);
            }
        }
    }
#endif // HAVE_STDIO_H

    // Try to open the file from the asset system

    void *iodata = NULL;
    if (!Android_JNI_FileOpen(&iodata, file, mode)) {
        return NULL;
    }

    SDL_IOStreamInterface iface;
    memset(&iface, 0, sizeof(SDL_IOStreamInterface));
	iface.version = sizeof(SDL_IOStreamInterface);
    iface.size = Android_JNI_FileSize;
    iface.seek = Android_JNI_FileSeek;
    iface.read = Android_JNI_FileRead;
    iface.write = Android_JNI_FileWrite;
    iface.close = Android_JNI_FileClose;

    iostr = SDL_OpenIO(&iface, iodata);
    if (!iostr) {
        iface.close(iodata);
    } else {
         SDL_PropertiesID props = SDL_GetIOProperties(iostr);
        if (props) {
            SDL_SetPointerProperty(props, SDL_PROP_IOSTREAM_ANDROID_AASSET_POINTER, iodata);
        }
    }

#elif defined(SDL_PLATFORM_WINDOWS)
    HANDLE handle = windows_file_open(file, mode);
    if (handle != INVALID_HANDLE_VALUE) {
        iostr = SDL_IOFromHandle(handle, mode, SDL_TRUE);
    }

#elif defined(HAVE_STDIO_H)
    {
        #if defined(SDL_PLATFORM_3DS)
        FILE *fp = N3DS_FileOpen(file, mode);
        #else
        FILE *fp = fopen(file, mode);
        #endif

        if (!fp) {
            SDL_SetError("Couldn't open %s: %s", file, strerror(errno));
        } else if (!IsRegularFileOrPipe(fp)) {
            fclose(fp);
            fp = NULL;
            SDL_SetError("%s is not a regular file or pipe", file);
        } else {
            iostr = SDL_IOFromFP(fp, SDL_TRUE);
        }
    }

#else
    SDL_SetError("SDL not compiled with stdio support");
#endif // !HAVE_STDIO_H

    return iostr;
}

SDL_IOStream *SDL_IOFromMem(void *mem, size_t size)
{
    if (!mem) {
        SDL_InvalidParamError("mem");
        return NULL;
    } else if (!size) {
        SDL_InvalidParamError("size");
        return NULL;
    }

    IOStreamMemData *iodata = (IOStreamMemData *) SDL_calloc(1, sizeof (*iodata));
    if (!iodata) {
        return NULL;
    }

    SDL_IOStreamInterface iface;
    memset(&iface, 0, sizeof(SDL_IOStreamInterface));
	iface.version = sizeof(SDL_IOStreamInterface);    iface.size = mem_size;
    iface.seek = mem_seek;
    iface.read = mem_read;
    iface.write = mem_write;
    iface.close = mem_close;

    iodata->base = (Uint8 *)mem;
    iodata->here = iodata->base;
    iodata->stop = iodata->base + size;

    SDL_IOStream *iostr = SDL_OpenIO(&iface, iodata);
    if (!iostr) {
        SDL_free(iodata);
    } else {
        SDL_PropertiesID *props = SDL_GetIOProperties(iostr);
        if (props) {
            //SDL_SetPointerProperty(props, SDL_PROP_IOSTREAM_MEMORY_POINTER, mem);
            //SDL_SetNumberProperty(props, SDL_PROP_IOSTREAM_MEMORY_SIZE_NUMBER, size);
			props->memoryPtr = mem;
			props->memorySize = size;
		}
    }
    return iostr;
}

SDL_IOStream *SDL_IOFromConstMem(const void *mem, size_t size)
{
    if (!mem) {
        SDL_InvalidParamError("mem");
        return NULL;
    } else if (!size) {
        SDL_InvalidParamError("size");
        return NULL;
    }

    IOStreamMemData *iodata = (IOStreamMemData *) SDL_calloc(1, sizeof (*iodata));
    if (!iodata) {
        return NULL;
    }

    SDL_IOStreamInterface iface;
    memset(&iface, 0, sizeof(SDL_IOStreamInterface));
	iface.version = sizeof(SDL_IOStreamInterface);
    iface.size = mem_size;
    iface.seek = mem_seek;
    iface.read = mem_read;
    // leave iface.write as NULL.
    iface.close = mem_close;

    iodata->base = (Uint8 *)mem;
    iodata->here = iodata->base;
    iodata->stop = iodata->base + size;

    SDL_IOStream *iostr = SDL_OpenIO(&iface, iodata);
    if (!iostr) {
        SDL_free(iodata);
    } else {
        SDL_PropertiesID *props = SDL_GetIOProperties(iostr);
        if (props) {
           // SDL_SetPointerProperty(props, SDL_PROP_IOSTREAM_MEMORY_POINTER, (void *)mem);
           // SDL_SetNumberProperty(props, SDL_PROP_IOSTREAM_MEMORY_SIZE_NUMBER, size);
			props->memoryPtr = mem;
			props->memorySize = size;
		}
    }
    return iostr;
}

typedef struct IOStreamDynamicMemData
{
    SDL_IOStream *stream;
    IOStreamMemData data;
    Uint8 *end;
} IOStreamDynamicMemData;

static Sint64 SDLCALL dynamic_mem_size(void *userdata)
{
    IOStreamDynamicMemData *iodata = (IOStreamDynamicMemData *) userdata;
    return mem_size(&iodata->data);
}

static Sint64 SDLCALL dynamic_mem_seek(void *userdata, Sint64 offset, SDL_IOWhence whence)
{
    IOStreamDynamicMemData *iodata = (IOStreamDynamicMemData *) userdata;
    return mem_seek(&iodata->data, offset, whence);
}

static size_t SDLCALL dynamic_mem_read(void *userdata, void *ptr, size_t size, SDL_IOStatus *status)
{
    IOStreamDynamicMemData *iodata = (IOStreamDynamicMemData *) userdata;
    return mem_io(&iodata->data, ptr, iodata->data.here, size);
}

static SDL_bool dynamic_mem_realloc(IOStreamDynamicMemData *iodata, size_t size)
{
    size_t chunksize = (size_t)SDL_GetIOProperties(iodata->stream)->chunkSize;
    if (!chunksize) {
        chunksize = 1024;
    }

    // We're intentionally allocating more memory than needed so it can be null terminated
    size_t chunks = (((iodata->end - iodata->data.base) + size) / chunksize) + 1;
    size_t length = (chunks * chunksize);
    Uint8 *base = (Uint8 *)SDL_realloc(iodata->data.base, length);
    if (!base) {
        return SDL_FALSE;
    }

    size_t here_offset = (iodata->data.here - iodata->data.base);
    size_t stop_offset = (iodata->data.stop - iodata->data.base);
    iodata->data.base = base;
    iodata->data.here = base + here_offset;
    iodata->data.stop = base + stop_offset;
    iodata->end = base + length;
    SDL_GetIOProperties(iodata->stream)->memoryPtr = base;
	
	//return SDL_SetPointerProperty(SDL_GetIOProperties(iodata->stream), SDL_PROP_IOSTREAM_DYNAMIC_MEMORY_POINTER, base);
}

static size_t SDLCALL dynamic_mem_write(void *userdata, const void *ptr, size_t size, SDL_IOStatus *status)
{
    IOStreamDynamicMemData *iodata = (IOStreamDynamicMemData *) userdata;
    if (size > (size_t)(iodata->data.stop - iodata->data.here)) {
        if (size > (size_t)(iodata->end - iodata->data.here)) {
            if (!dynamic_mem_realloc(iodata, size)) {
                return 0;
            }
        }
        iodata->data.stop = iodata->data.here + size;
    }
    return mem_io(&iodata->data, iodata->data.here, ptr, size);
}

static SDL_bool SDLCALL dynamic_mem_close(void *userdata)
{
    const IOStreamDynamicMemData *iodata = (IOStreamDynamicMemData *) userdata;
    void *mem = SDL_GetIOProperties(iodata->stream)->memoryPtr;
    if (mem) {
        SDL_free(mem);
    }
    SDL_free(userdata);
    return SDL_TRUE;
}
SDL_IOStream *SDL_IOFromDynamicMem(void)
{
    IOStreamDynamicMemData *iodata = (IOStreamDynamicMemData *) SDL_calloc(1, sizeof (*iodata));
    if (!iodata) {
        return NULL;
    }

    SDL_IOStreamInterface iface;
    memset(&iface, 0, sizeof(SDL_IOStreamInterface));
	iface.version = sizeof(SDL_IOStreamInterface);
	
    iface.size = dynamic_mem_size;
    iface.seek = dynamic_mem_seek;
    iface.read = dynamic_mem_read;
    iface.write = dynamic_mem_write;
    iface.close = dynamic_mem_close;

    SDL_IOStream *iostr = SDL_OpenIO(&iface, iodata);
    if (iostr) {
        iodata->stream = iostr;
    } else {
        SDL_free(iodata);
    }
    return iostr;
}

SDL_IOStatus SDL_GetIOStatus(SDL_IOStream *context)
{
    if (!context) {
        SDL_InvalidParamError("context");
        return SDL_IO_STATUS_ERROR;
    }
    return context->status;
}

SDL_IOStream *SDL_OpenIO(const SDL_IOStreamInterface *iface, void *userdata)
{
    if (!iface) {
        SDL_InvalidParamError("iface");
        return NULL;
    }
    if (iface->version < sizeof(*iface)) {
        // Update this to handle older versions of this interface
        SDL_SetError("Invalid interface, should be initialized with SDL_INIT_INTERFACE()");
        return NULL;
    }

    SDL_IOStream *iostr = (SDL_IOStream *)SDL_calloc(1, sizeof(*iostr));
    if (iostr) {
        //SDL_copyp(&iostr->iface, iface);
        iostr->userdata = userdata;
    }
    return iostr;
}

SDL_bool SDL_CloseIO(SDL_IOStream *iostr)
{
    SDL_bool result = SDL_TRUE;
    if (iostr) {
        if (iostr->iface.close) {
            result = iostr->iface.close(iostr->userdata);
        }
        //SDL_DestroyProperties(iostr->props);
        SDL_free(iostr);
    }
    return result;
}

// Load all the data from an SDL data stream
void *SDL_LoadFile_IO(SDL_IOStream *src, size_t *datasize, SDL_bool closeio)
{
    const int FILE_CHUNK_SIZE = 1024;
    Sint64 size, size_total = 0;
    size_t size_read;
    char *data = NULL, *newdata;
    SDL_bool loading_chunks = SDL_FALSE;

    if (!src) {
        SDL_InvalidParamError("src");
        goto done;
    }

    size = SDL_GetIOSize(src);
    if (size < 0) {
        size = FILE_CHUNK_SIZE;
        loading_chunks = SDL_TRUE;
    }
    if (size >= SIZE_MAX - 1) {
        goto done;
    }
    data = (char *)SDL_malloc((size_t)(size + 1));
    if (!data) {
        goto done;
    }

    size_total = 0;
    for (;;) {
        if (loading_chunks) {
            if ((size_total + FILE_CHUNK_SIZE) > size) {
                size = (size_total + FILE_CHUNK_SIZE);
                if (size >= SIZE_MAX - 1) {
                    newdata = NULL;
                } else {
                    newdata = (char *)SDL_realloc(data, (size_t)(size + 1));
                }
                if (!newdata) {
                    SDL_free(data);
                    data = NULL;
                    goto done;
                }
                data = newdata;
            }
        }

        size_read = SDL_ReadIO(src, data + size_total, (size_t)(size - size_total));
        if (size_read > 0) {
            size_total += size_read;
            continue;
        } else if (SDL_GetIOStatus(src) == SDL_IO_STATUS_NOT_READY) {
            // Wait for the stream to be ready
            SDL_Delay(1);
            continue;
        }

        // The stream status will remain set for the caller to check
        break;
    }

    data[size_total] = '\0';

done:
    if (datasize) {
        *datasize = (size_t)size_total;
    }
    if (closeio && src) {
        SDL_CloseIO(src);
    }
    return data;
}

void *_SDL_LoadFile(const char *file, size_t *datasize)
{
    SDL_IOStream *stream = SDL_IOFromFile(file, "rb");
    if (!stream) {
        if (datasize) {
            *datasize = 0;
        }
        return NULL;
    }
    return SDL_LoadFile_IO(stream, datasize, SDL_TRUE);
}

SDL_bool SDL_SaveFile_IO(SDL_IOStream *src, const void *data, size_t datasize, SDL_bool closeio)
{
    size_t size_written = 0;
    size_t size_total = 0;
    SDL_bool success = SDL_TRUE;

    if (!src) {
        SDL_InvalidParamError("src");
        goto done;
    }

    if (!data && datasize > 0) {
        SDL_InvalidParamError("data");
        goto done;
    }

    if (datasize > 0) {
        while (size_total < datasize) {
            size_written = SDL_WriteIO(src, ((const char *) data) + size_written, datasize - size_written);

            if (size_written <= 0) {
                if (SDL_GetIOStatus(src) == SDL_IO_STATUS_NOT_READY) {
                    // Wait for the stream to be ready
                    SDL_Delay(1);
                    continue;
                } else {
                    success = SDL_FALSE;
                    goto done;
                }
            }

            size_total += size_written;
        }
    }

done:
    if (closeio && src) {
        SDL_CloseIO(src);
    }

    return success;
}

SDL_bool SDL_SaveFile(const char *file, const void *data, size_t datasize)
{
    SDL_IOStream *stream = SDL_IOFromFile(file, "wb");
    if (!stream) {
        return SDL_FALSE;
    }
    return SDL_SaveFile_IO(stream, data, datasize, SDL_TRUE);
}

Sint64 SDL_GetIOSize(SDL_IOStream *context)
{
    if (!context) {
        return SDL_InvalidParamError("context");
    }
    if (!context->iface.size) {
        Sint64 pos, size;

        pos = SDL_SeekIO(context, 0, SDL_IO_SEEK_CUR);
        if (pos < 0) {
            return -1;
        }
        size = SDL_SeekIO(context, 0, SDL_IO_SEEK_END);

        SDL_SeekIO(context, pos, SDL_IO_SEEK_SET);
        return size;
    }
    return context->iface.size(context->userdata);
}

Sint64 SDL_SeekIO(SDL_IOStream *context, Sint64 offset, SDL_IOWhence whence)
{
    if (!context) {
        SDL_InvalidParamError("context");
        return -1;
    } else if (!context->iface.seek) {
        SDL_Unsupported();
        return -1;
    }
    return context->iface.seek(context->userdata, offset, whence);
}

Sint64 SDL_TellIO(SDL_IOStream *context)
{
    return SDL_SeekIO(context, 0, SDL_IO_SEEK_CUR);
}

size_t SDL_ReadIO(SDL_IOStream *context, void *ptr, size_t size)
{
    size_t bytes;

    if (!context) {
        SDL_InvalidParamError("context");
        return 0;
    } else if (!context->iface.read) {
        context->status = SDL_IO_STATUS_WRITEONLY;
        SDL_Unsupported();
        return 0;
    }

    context->status = SDL_IO_STATUS_READY;
    SDL_ClearError();

    if (size == 0) {
        return 0;
    }

    bytes = context->iface.read(context->userdata, ptr, size, &context->status);
    if (bytes == 0 && context->status == SDL_IO_STATUS_READY) {
        if (*SDL_GetError()) {
            context->status = SDL_IO_STATUS_ERROR;
        } else {
            context->status = SDL_IO_STATUS_EOF;
        }
    }
    return bytes;
}

size_t SDL_WriteIO(SDL_IOStream *context, const void *ptr, size_t size)
{
    size_t bytes;

    if (!context) {
        SDL_InvalidParamError("context");
        return 0;
    } else if (!context->iface.write) {
        context->status = SDL_IO_STATUS_READONLY;
        SDL_Unsupported();
        return 0;
    }

    context->status = SDL_IO_STATUS_READY;
    SDL_ClearError();

    if (size == 0) {
        return 0;
    }

    bytes = context->iface.write(context->userdata, ptr, size, &context->status);
    if ((bytes == 0) && (context->status == SDL_IO_STATUS_READY)) {
        context->status = SDL_IO_STATUS_ERROR;
    }
    return bytes;
}


SDL_bool SDL_FlushIO(SDL_IOStream *context)
{
    SDL_bool result = SDL_TRUE;

    if (!context) {
        return SDL_InvalidParamError("context");
    }

    context->status = SDL_IO_STATUS_READY;
    SDL_ClearError();

    if (context->iface.flush) {
        result = context->iface.flush(context->userdata, &context->status);
    }
    if (!result && (context->status == SDL_IO_STATUS_READY)) {
        context->status = SDL_IO_STATUS_ERROR;
    }
    return result;
}

// Functions for dynamically reading and writing endian-specific values

SDL_bool SDL_ReadU8Ptr(SDL_IOStream *src, Uint8 *value)
{
    Uint8 data = 0;
    SDL_bool result = SDL_FALSE;

    if (SDL_ReadIO(src, &data, sizeof(data)) == sizeof(data)) {
        result = SDL_TRUE;
    }
    if (value) {
        *value = data;
    }
    return result;
}

SDL_bool SDL_ReadS8(SDL_IOStream *src, Sint8 *value)
{
    Sint8 data = 0;
    SDL_bool result = SDL_FALSE;

    if (SDL_ReadIO(src, &data, sizeof(data)) == sizeof(data)) {
        result = SDL_TRUE;
    }
    if (value) {
        *value = data;
    }
    return result;
}

SDL_bool SDL_ReadU16LE(SDL_IOStream *src, Uint16 *value)
{
    Uint16 data = 0;
    SDL_bool result = SDL_FALSE;

    if (SDL_ReadIO(src, &data, sizeof(data)) == sizeof(data)) {
        result = SDL_TRUE;
    }
    if (value) {
        *value = SDL_SwapLE16(data);
    }
    return result;
}

SDL_bool SDL_ReadS16LE(SDL_IOStream *src, Sint16 *value)
{
    return SDL_ReadU16LE(src, (Uint16 *)value);
}

SDL_bool SDL_ReadU16BE(SDL_IOStream *src, Uint16 *value)
{
    Uint16 data = 0;
    SDL_bool result = SDL_FALSE;

    if (SDL_ReadIO(src, &data, sizeof(data)) == sizeof(data)) {
        result = SDL_TRUE;
    }
    if (value) {
        *value = SDL_SwapBE16(data);
    }
    return result;
}

SDL_bool SDL_ReadS16BE(SDL_IOStream *src, Sint16 *value)
{
    return SDL_ReadU16BE(src, (Uint16 *)value);
}

SDL_bool SDL_ReadU32LE(SDL_IOStream *src, Uint32 *value)
{
    Uint32 data = 0;
    SDL_bool result = SDL_FALSE;

    if (SDL_ReadIO(src, &data, sizeof(data)) == sizeof(data)) {
        result = SDL_TRUE;
    }
    if (value) {
        *value = SDL_SwapLE32(data);
    }
    return result;
}

SDL_bool SDL_ReadS32LE(SDL_IOStream *src, Sint32 *value)
{
    return SDL_ReadU32LE(src, (Uint32 *)value);
}

SDL_bool SDL_ReadU32BE(SDL_IOStream *src, Uint32 *value)
{
    Uint32 data = 0;
    SDL_bool result = SDL_FALSE;

    if (SDL_ReadIO(src, &data, sizeof(data)) == sizeof(data)) {
        result = SDL_TRUE;
    }
    if (value) {
        *value = SDL_SwapBE32(data);
    }
    return result;
}

SDL_bool SDL_ReadS32BE(SDL_IOStream *src, Sint32 *value)
{
    return SDL_ReadU32BE(src, (Uint32 *)value);
}

SDL_bool SDL_ReadU64LE(SDL_IOStream *src, Uint64 *value)
{
    Uint64 data = 0;
    SDL_bool result = SDL_FALSE;

    if (SDL_ReadIO(src, &data, sizeof(data)) == sizeof(data)) {
        result = SDL_TRUE;
    }
    if (value) {
        *value = SDL_SwapLE64(data);
    }
    return result;
}

SDL_bool SDL_ReadS64LE(SDL_IOStream *src, Sint64 *value)
{
    return SDL_ReadU64LE(src, (Uint64 *)value);
}

SDL_bool SDL_ReadU64BE(SDL_IOStream *src, Uint64 *value)
{
    Uint64 data = 0;
    SDL_bool result = SDL_FALSE;

    if (SDL_ReadIO(src, &data, sizeof(data)) == sizeof(data)) {
        result = SDL_TRUE;
    }
    if (value) {
        *value = SDL_SwapBE64(data);
    }
    return result;
}

SDL_bool SDL_ReadS64BE(SDL_IOStream *src, Sint64 *value)
{
    return SDL_ReadU64BE(src, (Uint64 *)value);
}

SDL_bool SDL_WriteU8Ptr(SDL_IOStream *dst, Uint8 value)
{
    return (SDL_WriteIO(dst, &value, sizeof(value)) == sizeof(value));
}

SDL_bool SDL_WriteS8(SDL_IOStream *dst, Sint8 value)
{
    return (SDL_WriteIO(dst, &value, sizeof(value)) == sizeof(value));
}

SDL_bool SDL_WriteU16LE(SDL_IOStream *dst, Uint16 value)
{
    const Uint16 swapped = SDL_SwapLE16(value);
    return (SDL_WriteIO(dst, &swapped, sizeof(swapped)) == sizeof(swapped));
}

SDL_bool SDL_WriteS16LE(SDL_IOStream *dst, Sint16 value)
{
    return SDL_WriteU16LE(dst, (Uint16)value);
}

SDL_bool SDL_WriteU16BE(SDL_IOStream *dst, Uint16 value)
{
    const Uint16 swapped = SDL_SwapBE16(value);
    return (SDL_WriteIO(dst, &swapped, sizeof(swapped)) == sizeof(swapped));
}

SDL_bool SDL_WriteS16BE(SDL_IOStream *dst, Sint16 value)
{
    return SDL_WriteU16BE(dst, (Uint16)value);
}

SDL_bool SDL_WriteU32LE(SDL_IOStream *dst, Uint32 value)
{
    const Uint32 swapped = SDL_SwapLE32(value);
    return (SDL_WriteIO(dst, &swapped, sizeof(swapped)) == sizeof(swapped));
}

SDL_bool SDL_WriteS32LE(SDL_IOStream *dst, Sint32 value)
{
    return SDL_WriteU32LE(dst, (Uint32)value);
}

SDL_bool SDL_WriteU32BE(SDL_IOStream *dst, Uint32 value)
{
    const Uint32 swapped = SDL_SwapBE32(value);
    return (SDL_WriteIO(dst, &swapped, sizeof(swapped)) == sizeof(swapped));
}

SDL_bool SDL_WriteS32BE(SDL_IOStream *dst, Sint32 value)
{
    return SDL_WriteU32BE(dst, (Uint32)value);
}

SDL_bool SDL_WriteU64LE(SDL_IOStream *dst, Uint64 value)
{
    const Uint64 swapped = SDL_SwapLE64(value);
    return (SDL_WriteIO(dst, &swapped, sizeof(swapped)) == sizeof(swapped));
}

SDL_bool SDL_WriteS64LE(SDL_IOStream *dst, Sint64 value)
{
    return SDL_WriteU64LE(dst, (Uint64)value);
}

SDL_bool SDL_WriteU64BE(SDL_IOStream *dst, Uint64 value)
{
    const Uint64 swapped = SDL_SwapBE64(value);
    return (SDL_WriteIO(dst, &swapped, sizeof(swapped)) == sizeof(swapped));
}

SDL_bool SDL_WriteS64BE(SDL_IOStream *dst, Sint64 value)
{
    return SDL_WriteU64BE(dst, (Uint64)value);
}
