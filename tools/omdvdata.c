/*
 * omdvdata — build the 'OMdv' 128 resource data file (OMdvData) for the
 * USBMIDI9 OMS driver, from the Target-A PEF container.
 *
 * OMdvData = 4-byte big-endian PEF length + the exact PEF container
 * bytes. Format reference: the Roland SC-8850 OMS driver's 'OMdv'
 * resource (4-byte length header `00 00 <len>` + a "Joy!peffpwpc" PEF
 * container) — see docs/g4-handoff.md, "The 'OMdv' resource".
 *
 * Usage:
 *   omdvdata <pef-in> [omdvdata-out]
 * With one argument the output is written next to the input (same
 * folder, name "OMdvData") — path separators: ':' (classic Mac OS) or
 * '/' (Unix). With two arguments the second names the output file.
 *
 * Validation (all four must pass; exit status 1 otherwise):
 *   - the raw PEF begins with "Joy!peffpwpc"
 *   - OMdvData size = PEF size + 4
 *   - OMdvData[0:4] = big-endian PEF size
 *   - OMdvData[4:16] = "Joy!peffpwpc"
 *
 * The OMS length header is 16-bit (`00 00 <len>`), so the PEF must be
 * at most 65535 bytes; anything larger is rejected (the OMS shim PEF
 * is far below that).
 *
 * Builds anywhere: host tests compile it with the repo's strict C89
 * flags; on the G4 it compiles with CodeWarrior/MSL as a console app
 * (stdio only, no Mac Toolbox calls).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char kMagic[] = "Joy!peffpwpc";

/* Verify the written OMdvData against the four checks and print them.
 * Returns 0 when all pass, 1 otherwise. */
static int verify_output(const char *path, long pef_size,
                         const unsigned char hdr[4])
{
    FILE *f;
    long size;
    unsigned char buf[16];
    int i;

    f = fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "omdvdata: cannot reopen %s\n", path);
        return 1;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 1;
    }
    size = ftell(f);
    rewind(f);
    if (fread(buf, 1, 16, f) != 16) {
        fclose(f);
        return 1;
    }
    fclose(f);

    printf("  raw PEF magic [0:12]: \"Joy!peffpwpc\"        OK\n");
    if (size != pef_size + 4) {
        fprintf(stderr, "omdvdata: %s size %ld != PEF size + 4 (%ld)\n",
                path, size, pef_size + 4);
        return 1;
    }
    printf("  OMdvData size: %ld bytes (PEF size + 4)      OK\n", size);
    for (i = 0; i < 4; i++) {
        if (buf[i] != hdr[i]) {
            fprintf(stderr, "omdvdata: %s length header mismatch\n", path);
            return 1;
        }
    }
    printf("  OMdvData[0:4]: 00 00 %02x %02x (big-endian size)  OK\n",
           hdr[2], hdr[3]);
    for (i = 0; i < 12; i++) {
        if (buf[4 + i] != (unsigned char)kMagic[i]) {
            fprintf(stderr, "omdvdata: %s magic mismatch at [4:16]\n", path);
            return 1;
        }
    }
    printf("  OMdvData[4:16]: \"Joy!peffpwpc\"              OK\n");
    return 0;
}

int main(int argc, char **argv)
{
    FILE *in, *out;
    const char *inpath, *outpath;
    long pef_size;
    int i, c;
    unsigned char hdr[4];

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: omdvdata <pef-in> [omdvdata-out]\n");
        return 1;
    }
    inpath = argv[1];

    in = fopen(inpath, "rb");
    if (in == NULL) {
        fprintf(stderr, "omdvdata: cannot open %s\n", inpath);
        return 1;
    }
    if (fseek(in, 0, SEEK_END) != 0) {
        fclose(in);
        fprintf(stderr, "omdvdata: cannot seek %s\n", inpath);
        return 1;
    }
    pef_size = ftell(in);
    if (pef_size < 16) {
        fclose(in);
        fprintf(stderr, "omdvdata: %s is %ld bytes (PEF must be >= 16)\n",
                inpath, pef_size);
        return 1;
    }
    rewind(in);

    for (i = 0; i < (int)sizeof(kMagic) - 1; i++) {
        c = fgetc(in);
        if (c != (unsigned char)kMagic[i]) {
            fclose(in);
            fprintf(stderr,
                    "omdvdata: %s does not start with \"Joy!peffpwpc\" "
                    "(not a PEF container)\n", inpath);
            return 1;
        }
    }
    rewind(in);

    if (pef_size > 0xFFFFL) {
        fclose(in);
        fprintf(stderr,
                "omdvdata: %s is %ld bytes; OMS 16-bit length header "
                "accepts at most 65535\n", inpath, pef_size);
        return 1;
    }

    if (argc == 3) {
        outpath = argv[2];
    } else {
        /* OMdvData next to the input: strip the last path component. */
        const char *p = strrchr(inpath, ':');
        size_t n;
        char *buf;
        if (p == NULL)
            p = strrchr(inpath, '/');
        n = (p == NULL) ? 0 : (size_t)(p - inpath + 1);
        buf = (char *)malloc(n + 9);
        if (buf == NULL) {
            fclose(in);
            fprintf(stderr, "omdvdata: out of memory\n");
            return 1;
        }
        if (n > 0)
            memcpy(buf, inpath, n);
        memcpy(buf + n, "OMdvData", 9); /* 8 chars + NUL */
        outpath = buf;
    }

    out = fopen(outpath, "wb");
    if (out == NULL) {
        fclose(in);
        fprintf(stderr, "omdvdata: cannot create %s\n", outpath);
        return 1;
    }

    hdr[0] = 0x00;
    hdr[1] = 0x00;
    hdr[2] = (unsigned char)((pef_size >> 8) & 0xFF);
    hdr[3] = (unsigned char)(pef_size & 0xFF);
    if (fwrite(hdr, 1, 4, out) != 4) {
        fclose(in);
        fclose(out);
        fprintf(stderr, "omdvdata: write failed on %s\n", outpath);
        return 1;
    }
    for (i = 0; i < pef_size; i++) {
        c = fgetc(in);
        if (c == EOF || fputc(c, out) == EOF) {
            fclose(in);
            fclose(out);
            fprintf(stderr, "omdvdata: copy failed on %s\n", outpath);
            return 1;
        }
    }
    if (fclose(out) != 0 || fclose(in) != 0) {
        fprintf(stderr, "omdvdata: close failed\n");
        return 1;
    }

    printf("OMdvData: %s\n", outpath);
    return verify_output(outpath, pef_size, hdr);
}
