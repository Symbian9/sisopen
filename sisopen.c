#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <errno.h>

#ifndef NOZLIB
#include <zlib.h>
#endif

#include "antigetopt.h"
#include "langtab.h"

#define SISOPEN_ERRLEN 1024

#define SIS_OPT_UNICODE 0x01
#define SIS_OPT_DISTRIBUTABLE 0x02
#define SIS_OPT_NOCOMPRESS 0x08
#define SIS_OPT_SHUTDOWNAPPS 0x10

#define SIS_TYPE_SA 0x00
#define SIS_TYPE_SY 0x01
#define SIS_TYPE_SO 0x02
#define SIS_TYPE_SC 0x03
#define SIS_TYPE_SP 0x04
#define SIS_TYPE_SU 0x05

#define SIS_FILE_SIMPLE     0x00
#define SIS_FILE_MULTILANG  0x01
#define SIS_FILE_OPTIONS    0x02
#define SIS_FILE_IF         0x03
#define SIS_FILE_ELSEIF     0x04
#define SIS_FILE_ELSE       0x05
#define SIS_FILE_ENDIF      0x06

#define SIS_FILETYPE_STANDARD   0x00
#define SIS_FILETYPE_TEXT       0x01
#define SIS_FILETYPE_COMPONENT  0x02
#define SIS_FILETYPE_RUN        0x03
#define SIS_FILETYPE_NOTEXISTS  0x04
#define SIS_FILETYPE_OPEN       0x05

#define SIS_NOTUSED(V) ((void) V)

static char *sisFileRecordTypeTab[] = {"simple","multilang","options","if","elseif","else","endif"};
static char *sisFileTypeTab[] = {"standard","text","component","run during installation/removal","file does not exist, will be created when the app is run","open file"};

static int lendian; /* set to 1 if it's little endian.
                Sisopen detects endianess at runtime */
static int optExtract=0;
static int optVerbose=0;
static int flagNocompr=0; /* flag set to 1 if SIS_OPT_NOCOMPRESS is present */

struct sishdr {
    unsigned int uid1;
    unsigned int uid2;
    unsigned int uid3;
    unsigned int uid4;
    unsigned short cksum;
    unsigned short languages;
    unsigned short files;
    unsigned short requisities;
    unsigned short instlang;
    unsigned short instfiles;
    unsigned short instdrive;
    unsigned short capabilities;
    unsigned int installerver;
    unsigned short options;
    unsigned short type;
    unsigned short major;
    unsigned short minor;
    unsigned int variant;
    unsigned int langoff;
    unsigned int fileoff;
    unsigned int reqoff;
    unsigned int certoff;
    unsigned int compnameoff;
    /* EPOC release 6 only fields follow */
#define EPOC6_HDR_TAIL_LEN 16
    unsigned int signoff;
    unsigned int capaoff;
    unsigned int instspace;
    unsigned int maxinstspace;
};

struct filerecord {
    unsigned int type;
    unsigned int details;
    unsigned int srcnamelen;
    unsigned int srcnameoff;
    unsigned int dstnamelen;
    unsigned int dstnameoff;
};

static unsigned int sis32toh(unsigned int val) {
    return val;
}

static unsigned short sis16toh(unsigned short val) {
    return val;
}

static void verbose(const char *fmt, ...)
{
    va_list ap;

    if (optVerbose) {
        va_start(ap, fmt);
        vprintf(fmt, ap);
        va_end(ap);
    }
}

int sisRead(FILE *fp, void *ptr, int len, char *err, int errlen)
{
    int nread;

    nread = fread(ptr, 1, len, fp);
    if (nread != len) {
        if (ferror(fp)) {
            snprintf(err,errlen,"Error reading from file: %s", strerror(errno));
            return 1;
        } else {
            int offset = ftell(fp);
            snprintf(err,errlen,"Unexpected EOF or short read (%d bytes of %d retured at offset %d)", nread, len, offset);
            return 1;
        }
    }
    return 0;
}

int sisSeek(FILE *fp, int off, char *err, int errlen)
{
    if (fseek(fp,off,SEEK_SET) == -1) {
        snprintf(err,errlen,"seeking: %s", strerror(errno));
        return 1;
    }
    return 0;
}

#if 0 /* debugging stuff */
static int dumpBytes(FILE *fp, int count)
{
    unsigned char c;
    int i = 0;

    while(count--) {
        i++;
        c = fgetc(fp);
        printf("%02x ", c);
        if ((i % 16) == 0) printf("\n");
        else if ((i % 8) == 0) printf("  --  ");
    }
    if ((i%16)) printf("\n");
    return 0;
}

static int dumpBytesAt(FILE *fp, int count, int off)
{
    long orig = ftell(fp);
    sisSeek(fp, off, NULL, 0);
    dumpBytes(fp, count);
    sisSeek(fp, orig, NULL, 0);
    return 0;
}
#endif



int sisReadOffset(FILE *fp, void *ptr, int len, int off, char *err, int errlen)
{
    long oldpos = ftell(fp);

    if (oldpos == -1 || fseek(fp,off,SEEK_SET) == -1) {
        snprintf(err,errlen,"seeking: %s", strerror(errno));
        return 1;
    }
    if (sisRead(fp,ptr,len,err,errlen)) return 1;
    /* Restore the old position */
    if (fseek(fp,oldpos,SEEK_SET) == -1) {
        snprintf(err,errlen,"seeking: %s", strerror(errno));
        return 1;
    }
    return 0;
}

char *sisReadOffsetAlloc(FILE *fp, int len, int off, char *err, int errlen)
{
    unsigned char *buf = malloc(len+1);

    if (!buf) {
        snprintf(err,errlen,"Out of memory");
        return NULL;
    }
    buf[len] = '\0';
    if (sisReadOffset(fp,buf,len,off,err,errlen)) {
        free(buf);
        return NULL;
    }
    return buf;
}

static int langaugesSection(FILE *fp, struct sishdr *hdr, char *err, int errlen)
{
    int languages = hdr->languages;

    if (sisSeek(fp, hdr->langoff, err, errlen)) return 1;
    printf("\nLanguages\n  ");
    while(languages--) {
        unsigned short lang;

        if (sisRead(fp, &lang, 2, err, errlen)) return 1;
        lang = sis16toh(lang);
        if (lang < sizeof(sisLangTab)/sizeof(char*)) {
            printf("%s ", sisLangTab[lang]);
        } else {
            printf("Unknown language code %d ", lang);
        }
    }
    printf("\n");
    return 0;
}

static char *fileRecordTypeStr(unsigned int filetype) {
    if (filetype < sizeof(sisFileRecordTypeTab)/sizeof(char*)) {
        return sisFileRecordTypeTab[filetype];
    } else {
        return "unknown";
    }
}

static char *fileTypeStr(unsigned int filetype) {
    if (filetype < sizeof(sisFileTypeTab)/sizeof(char*)) {
        return sisFileTypeTab[filetype];
    } else {
        return "unknown";
    }
}

static void uni2ascii(char *s, int len)
{
    int i;

    for(i = 0; i < len; i+=2) {
        s[i/2] = s[i];
    }
    s[len/2]='\0';
}

static int extractFile(FILE *fp, int len, int origlen, int off, char *name, char *err, int errlen)
#ifndef NOZLIB
{
    char *basename = strrchr(name,'\\');
    char *zdata, *data;
    uLongf destlen; /* ulongf is a zlib type */
    int retval;
    FILE *dstfp;

    if (basename && strrchr(basename,'/')) basename = strrchr(basename,'/');
    if (basename)
        basename++;
    else
        basename = name;
    printf("Extracting %s (%d bytes compressed, offset %d)\n", basename, len, off);
    if ((zdata = sisReadOffsetAlloc(fp, len, off, err, errlen)) == NULL)
        return 1;
    if (origlen && (data = malloc(origlen)) == NULL) {
        snprintf(err, errlen, "Out of memory");
        return 1;
    }
    if (origlen == 0 || flagNocompr) {
        data = zdata;
        origlen = len;
    } else {
        destlen = origlen;
        if ((retval = uncompress(data, &destlen, zdata, len)) != Z_OK) {
            snprintf(err, errlen, "zlib reported error trying to uncompress (error %d)",retval);
            free(zdata);
            free(data);
            return 1;
        }
        if (destlen != (unsigned)origlen) {
            free(zdata);
            free(data);
            snprintf(err, errlen, "uncompressed file length does not match!");
            return 1;
        }
        free(zdata);
    }

    /* Time to write the file content on disk */
    dstfp = fopen(basename, "w");
    if (!dstfp) {
        free(data);
        snprintf(err, errlen, "error opening file for writing: %s\n",
            strerror(errno));
        return 1;
    }
    fwrite(data, origlen, 1, dstfp);
    fclose(dstfp);

    /* Done */
    free(data);
    return 0;
}
#else
{
    SIS_NOTUSED(fp);
    SIS_NOTUSED(len);
    SIS_NOTUSED(origlen);
    SIS_NOTUSED(off);
    SIS_NOTUSED(name);
    SIS_NOTUSED(err);
    SIS_NOTUSED(errlen);
    fprintf(stderr, "Sorry, this sisopen binary is compiled without zlib support, so file extraction is not supported.\n");
    exit(1);
}
#endif

static int simpleFile(FILE *fp, struct sishdr *hdr, int filenum, int numlangs, char *err, int errlen)
{
    struct filerecord file;
    char *srcname = NULL;
    char *dstname = NULL;
    int isepoc6 = (hdr->uid2 == 0x10003A12);
    int i;
    unsigned int mimelen, mimeoff;
    unsigned int *filelen = NULL, *fileoff = NULL, *origlen = NULL;

    file.type = sis32toh(file.type);
    file.details = sis32toh(file.details);
    file.srcnamelen = sis32toh(file.srcnamelen);
    file.srcnameoff = sis32toh(file.srcnameoff);
    file.dstnamelen = sis32toh(file.dstnamelen);
    file.dstnameoff = sis32toh(file.dstnameoff);

    if (sisRead(fp, &file, sizeof(file), err, errlen)) return -1;
    verbose("    file type: %s\n", fileTypeStr(file.type));
    verbose("    file details: %d\n", file.details);
    if ((srcname = sisReadOffsetAlloc(fp, file.srcnamelen, file.srcnameoff, err, errlen)) == NULL) goto err;
    uni2ascii(srcname,file.srcnamelen);
    verbose("    source file name: %s\n", srcname);

    if ((dstname = sisReadOffsetAlloc(fp, file.dstnamelen, file.dstnameoff, err, errlen)) == NULL) {
        goto err;
    }
    uni2ascii(dstname,file.dstnamelen);
    verbose("    destination file name: %s\n", dstname);
    verbose("    this file is available in %d language(s)\n", numlangs);

    if ((filelen = malloc(numlangs*sizeof(int))) == NULL) goto oom;
    if ((fileoff = malloc(numlangs*sizeof(int))) == NULL) goto oom;

    /* Read len/offset information */
    for (i = 0; i < numlangs; i++) {
        if (sisRead(fp, &filelen[i], 4, err, errlen)) goto err;
        filelen[i] = sis32toh(filelen[i]);
        verbose("      len[%d]: %d bytes\n", i+1, filelen[i]);
    }
    for (i = 0; i < numlangs; i++) {
        if (sisRead(fp, &fileoff[i], 4, err, errlen)) goto err;
        fileoff[i] = sis32toh(fileoff[i]);
        verbose("      file language %d is at offset %d\n", i+1, fileoff[i]);
    }
    if (isepoc6) {
        if ((origlen = malloc(numlangs*sizeof(int))) == NULL) goto oom;
        for (i = 0; i < numlangs; i++) {
            if (sisRead(fp, &origlen[i], 4, err, errlen)) goto err;
            verbose("      original len[%d]: %d bytes\n", i+1, origlen[i]);
        }
        if (sisRead(fp, &mimelen, 4, err, errlen)) goto err;
        if (sisRead(fp, &mimeoff, 4, err, errlen)) goto err;
    }

    /* Show file info in non verbose mode */
    if (!optVerbose) {
        char c=' ';
        switch(file.type) {
            case SIS_FILETYPE_STANDARD:
                if (numlangs == 1)
                    c='f';
                else
                    c='m';
                break;
            case SIS_FILETYPE_TEXT: c='t'; break;
            case SIS_FILETYPE_COMPONENT: c='c'; break;
            case SIS_FILETYPE_RUN: c='r'; break;
            case SIS_FILETYPE_NOTEXISTS: c='x'; break;
            case SIS_FILETYPE_OPEN: c='o'; break;
        }
        printf("%03d %c %-63s", filenum,c,dstname[0] ? dstname : srcname);
        if (origlen) printf(" %10d", origlen[0]);
        printf("\n");
    }

    /* Extract files if needed */
    if (optExtract) {
        for (i = 0; i < numlangs; i++) {
            char *ename = dstname[0] ? dstname : srcname;
            if (file.type != SIS_FILETYPE_NOTEXISTS) {
                if (extractFile(fp, filelen[i], origlen ? origlen[i] : 0,
                    fileoff[i], ename, err, errlen))
                    goto err;
            }
        }
    }
    verbose("\n");
    free(filelen);
    free(fileoff);
    free(srcname);
    free(dstname);
    if (origlen) free(origlen);
    return 0;

oom:
    snprintf(err,errlen,"Out of memory");
err:
    if (filelen) free(filelen);
    if (fileoff) free(fileoff);
    if (origlen) free(origlen);
    if (srcname) free(srcname);
    if (dstname) free(dstname);
    return 1;
}

static void elseFile(void)
{
    printf("[else]\n");
}

static void endifFile(void)
{
    printf("[endif]\n");
}

static int condAttribute(FILE *fp, char *err, int errlen)
{
    unsigned int attribtype, unused;

    if (sisRead(fp, &attribtype, 4, err, errlen)) return 1;
    attribtype = sis32toh(attribtype);
    /* Read the next unused 4 bytes */
    if (sisRead(fp, &unused, 4, err, errlen)) return 1;
    if (attribtype >= 0x2000) {
        printf("option %d", attribtype-0x2000);
    } else {
        switch(attribtype) {
        case 0x00: printf("Manufacturer"); break;
        case 0x01: printf("ManufacturerHardwareRev"); break;
        case 0x02: printf("ManufacturerSoftwareRev"); break;
        case 0x03: printf("ManufacturerSoftwareBuild"); break;
        case 0x04: printf("Model"); break;
        case 0x05: printf("MachineUID"); break;
        case 0x06: printf("DeviceFamily"); break;
        case 0x07: printf("DeviceFamilyRev"); break;
        case 0x08: printf("CPU type"); break;
        case 0x09: printf("CPU arch"); break;
        case 0x0a: printf("CPU ABI"); break;
        case 0x0b: printf("CPU speed"); break;
        case 0x0e: printf("System Tick Period"); break;
        case 0x0f: printf("Total RAM"); break;
        case 0x10: printf("Free RAM"); break;
        case 0x11: printf("Total ROM"); break;
        case 0x12: printf("Memory Page Size"); break;
        case 0x15: printf("Power backup"); break;
        case 0x18: printf("Keyboard"); break;
        case 0x19: printf("Keyboard device key"); break;
        case 0x1a: printf("Keyboard application key"); break;
        case 0x1b: printf("Keyboard click"); break;
        case 0x1e: printf("Keyboard clickVolMax"); break;
        case 0x1f: printf("Screen width pixel"); break;
        case 0x20: printf("Screen height pixel"); break;
        case 0x21: printf("Screen width twips"); break;
        case 0x22: printf("Screen height twips"); break;
        case 0x23: printf("Display colors"); break;
        case 0x26: printf("Display max contrast"); break;
        case 0x27: printf("Backlight"); break;
        case 0x29: printf("Pen"); break;
        case 0x2a: printf("PenX"); break;
        case 0x2b: printf("PenY"); break;
        case 0x2c: printf("Pen display on"); break;
        case 0x2d: printf("Pen click"); break;
        case 0x30: printf("Pen volume max"); break;
        case 0x31: printf("Mouse"); break;
        case 0x32: printf("MouseX"); break;
        case 0x33: printf("MouseY"); break;
        case 0x37: printf("Mouse buttons"); break;
        case 0x3a: printf("Case switch"); break;
        case 0x3d: printf("Leds"); break;
        case 0x3f: printf("Integrated phone"); break;
        case 0x41: printf("Display brightness max"); break;
        case 0x42: printf("Keyboard backlight state"); break;
        case 0x43: printf("Accessory power"); break;
        case 0x59: printf("Number of supported HAL attributes"); break;
        case 0x1000: printf("Machine language"); break;
        case 0x1001: printf("Remote install"); break;
        default: printf("attribute %04x", attribtype); break;
        }
    }
    return 0;
}

static int condNumber(FILE *fp, char *err, int errlen)
{
    unsigned int value, unused;

    if (sisRead(fp, &value, 4, err, errlen)) return 1;
    value = sis32toh(value);
    if (sisRead(fp, &unused, 4, err, errlen)) return 1;
    printf("0x%04x", value);
    return 0;
}

static int condString(FILE *fp, char *err, int errlen)
{
    unsigned int len, off;
    char *str;

    if (sisRead(fp, &len, 4, err, errlen)) return 1;
    if (sisRead(fp, &off, 4, err, errlen)) return 1;
    len = sis32toh(len);
    off = sis32toh(off);
    if ((str = sisReadOffsetAlloc(fp, len, off, err, errlen)) == NULL) return 1;
    uni2ascii(str,len);
    printf("%s", str);
    free(str);
    return 0;
}

static int condExpr(FILE *fp, char *err, int errlen)
{
    unsigned int condtype;

    if (sisRead(fp, &condtype, 4, err, errlen)) return 1;
    condtype = sis32toh(condtype);
    switch(condtype) {
        case 0x00: /* Attribute == Value */
            if (condExpr(fp, err, errlen)) return 1;
            printf(" == ");
            if (condExpr(fp, err, errlen)) return 1;
            break;
        case 0x01: /* Attribute != Value */
            if (condExpr(fp, err, errlen)) return 1;
            printf(" != ");
            if (condExpr(fp, err, errlen)) return 1;
            break;
        case 0x02: /* Attribute > Value */
            if (condExpr(fp, err, errlen)) return 1;
            printf(" > ");
            if (condExpr(fp, err, errlen)) return 1;
            break;
        case 0x03: /* Attribute < Value */
            if (condExpr(fp, err, errlen)) return 1;
            printf(" < ");
            if (condExpr(fp, err, errlen)) return 1;
            break;
        case 0x04: /* Attribute >= Value */
            if (condExpr(fp, err, errlen)) return 1;
            printf(" >= ");
            if (condExpr(fp, err, errlen)) return 1;
            break;
        case 0x05: /* Attribute <= Value */
            if (condExpr(fp, err, errlen)) return 1;
            printf(" <= ");
            if (condExpr(fp, err, errlen)) return 1;
            break;
        case 0x06: /* expr AND expr */
            if (condExpr(fp, err, errlen)) return 1;
            printf(" AND ");
            if (condExpr(fp, err, errlen)) return 1;
            break;
        case 0x07: /* expr OR expr */
            if (condExpr(fp, err, errlen)) return 1;
            printf(" OR ");
            if (condExpr(fp, err, errlen)) return 1;
            break;
        case 0x08: /* ??? appcap(UID, Capability) */
        case 0x09: /* exists(UID, Capability) */
            printf("EXISTS(");
            if (condExpr(fp, err, errlen)) return 1;
            printf(")");
            break;
        case 0x0a: /* devcap(Capability) */
            printf("DEVCAP(");
            if (condExpr(fp, err, errlen)) return 1;
            printf(")");
            break;
        case 0x0b: /* NOT(expr) */
            printf("NOT(");
            if (condExpr(fp, err, errlen)) return 1;
            printf(")");
            break;
        case 0x0c: /* String */
            if (condString(fp, err, errlen)) return 1;
            break;
        case 0x0d: /* Attribute */
            if (condAttribute(fp, err, errlen)) return 1;
            break;
        case 0x0e:
            if (condNumber(fp, err, errlen)) return 1;
            break;
        default:
            snprintf(err, errlen, "Unknown conditional type %04x", condtype);
            return 1;
            break;
    }
    return 0;
}

static int conditional(FILE *fp, char *err, int errlen)
{
    unsigned int condlen;

    if (sisRead(fp, &condlen, 4, err, errlen)) return 1;
    condlen = sis32toh(condlen);
    return condExpr(fp, err, errlen);
}

static int ifFile(FILE *fp, char *err, int errlen)
{
    printf("[if (");
    if (conditional(fp, err, errlen)) return 1;
    printf(")]\n");
    return 0;
}

static int elseifFile(FILE *fp, char *err, int errlen)
{
    printf("[else if (");
    if (conditional(fp, err, errlen)) return 1;
    printf(")]\n");
    return 0;
}

static int optionsFile(FILE *fp, char *err, int errlen)
{
    unsigned int numopt, j;
    unsigned char selected[16];

    if (sisRead(fp, &numopt, 4, err, errlen)) return 1;
    numopt = sis32toh(numopt);
    for (j = 0; j < numopt; j++) {
        unsigned int optlen, optoff;
        char *optstr;

        if (sisRead(fp, &optlen, 4, err, errlen)) return 1;
        if (sisRead(fp, &optoff, 4, err, errlen)) return 1;
        optlen = sis32toh(optlen);
        optoff = sis32toh(optoff);
        if ((optstr = sisReadOffsetAlloc(fp, optlen, optoff, err, errlen)) == NULL) return 1;
        uni2ascii(optstr,optlen);
        printf("  option %d: %s\n", numopt-j, optstr);
        free(optstr);
    }
    /* Read the "selected options" section, but discard it */
    if (sisRead(fp, selected, 16, err, errlen)) return 1;
    return 0;
}

static int filesSection(FILE *fp, struct sishdr *hdr, char *err, int errlen)
{
    int j;

    if (sisSeek(fp, hdr->fileoff, err, errlen)) return 1;
    printf("\nFiles\n");
    for (j = 0; j < hdr->files; j++) {
        unsigned int recordtype;

        if (sisRead(fp, &recordtype, 4, err, errlen)) return 1;
        recordtype = sis32toh(recordtype);
        verbose("  FILE %d type %s\n",j+1,fileRecordTypeStr(recordtype));
        switch(recordtype) {
        case SIS_FILE_SIMPLE:
            if (simpleFile(fp, hdr, j, 1, err, errlen)) return 1;
            break;
        case SIS_FILE_MULTILANG:
            if (simpleFile(fp, hdr, j, hdr->languages, err, errlen)) return 1;
            break;
        case SIS_FILE_OPTIONS:
            if (optionsFile(fp, err, errlen)) return 1;
            break;
        case SIS_FILE_IF:
            if (ifFile(fp, err, errlen)) return 1;
            break;
        case SIS_FILE_ELSEIF:
            if (elseifFile(fp, err, errlen)) return 1;
            break;
        case SIS_FILE_ELSE: elseFile(); break;
        case SIS_FILE_ENDIF: endifFile(); break;
        default:
            snprintf(err, errlen, "Unknown file record type %d", recordtype);
            return 1;
            break;
        }
    }
    printf("\n");
    return 0;
}

static int sisopen(char *filename, FILE *fp, char *err, int errlen)
{
    struct sishdr hdr;
    int epocrelease = 0;

    /* Header */

    if (sisRead(fp, &hdr, sizeof(hdr)-EPOC6_HDR_TAIL_LEN, err, errlen))
        return 1;
    hdr.uid1 = sis32toh(hdr.uid1);
    hdr.uid2 = sis32toh(hdr.uid2);
    hdr.uid3 = sis32toh(hdr.uid3);
    hdr.uid4 = sis32toh(hdr.uid4);
    hdr.cksum = sis16toh(hdr.cksum);
    hdr.languages = sis16toh(hdr.languages);
    hdr.files = sis16toh(hdr.files);
    hdr.requisities = sis16toh(hdr.requisities);
    hdr.instlang = sis16toh(hdr.instlang);
    hdr.instfiles = sis16toh(hdr.instfiles);
    hdr.instdrive = sis16toh(hdr.instdrive);
    hdr.capabilities = sis16toh(hdr.capabilities);
    hdr.installerver = sis32toh(hdr.installerver);
    hdr.options = sis16toh(hdr.options);
    hdr.type = sis16toh(hdr.type);
    hdr.major = sis16toh(hdr.major);
    hdr.minor = sis16toh(hdr.minor);
    hdr.variant = sis32toh(hdr.variant);
    hdr.langoff = sis32toh(hdr.langoff);
    hdr.fileoff = sis32toh(hdr.fileoff);
    hdr.reqoff = sis32toh(hdr.reqoff);
    hdr.certoff = sis32toh(hdr.certoff);
    hdr.compnameoff = sis32toh(hdr.compnameoff);

    if (hdr.uid3 == 0x10000419) {
        printf("%s: SIS header detected\n", filename);
    } else {
        snprintf(err, errlen, "file corrupted or not a SIS file");
        return 1;
    }
    printf("  application UID: 0x%04X\n", hdr.uid1);
    verbose("  UID2: %04X", hdr.uid2);
    switch(hdr.uid2) {
    case 0x1000006D:
        epocrelease = 5;
        verbose(" (EPOC release 3,4,5)");
        break;
    case 0x10003A12:
        epocrelease = 6;
        verbose(" (EPOC release 6)");
        break;
    }
    /* If it's an EPOC release 6 file read the rest of the header */
    if (epocrelease == 6) {
        unsigned char *tail = ((unsigned char*)&hdr)+(sizeof(hdr)-EPOC6_HDR_TAIL_LEN);
        if (sisRead(fp, tail, EPOC6_HDR_TAIL_LEN, err, errlen)) return 1;
        hdr.signoff = sis32toh(hdr.signoff);
        hdr.capaoff = sis32toh(hdr.capaoff);
        hdr.instspace = sis32toh(hdr.instspace);
        hdr.maxinstspace = sis32toh(hdr.maxinstspace);
    }

    /* Show header information */
    verbose("\n");
    verbose("  installer version required: %d\n", hdr.installerver);
    verbose("  number of languages in this SIS: %d\n", hdr.languages);
    verbose("  number of files in this SIS: %d\n", hdr.files);
    verbose("  options:");
    if (hdr.options & SIS_OPT_UNICODE) verbose(" unicode");
    if (hdr.options & SIS_OPT_DISTRIBUTABLE) verbose(" distributable");
    if (hdr.options & SIS_OPT_NOCOMPRESS) {
        flagNocompr = 1;
        verbose(" nocompress");
    }
    if (hdr.options & SIS_OPT_SHUTDOWNAPPS) verbose(" shutdownapps");
    if (hdr.options == 0) verbose("none");
    verbose("\n");
    verbose("  package type: ");
    switch(hdr.type) {
    case SIS_TYPE_SA: verbose("application"); break;
    case SIS_TYPE_SY: verbose("shared/system component/library"); break;
    case SIS_TYPE_SO: verbose("optional component"); break;
    case SIS_TYPE_SC: verbose("configuration"); break;
    case SIS_TYPE_SP: verbose("patch"); break;
    case SIS_TYPE_SU: verbose("upgrade"); break;
    default: verbose("unknown (%d)", hdr.type); break;
    }
    verbose("\n");
    printf("  application version: %d.%02d\n", hdr.major, hdr.minor);
    verbose("  variant: %d\n", hdr.variant);
    verbose("  languages section is at: %d\n", hdr.langoff);
    verbose("  files section is at    : %d\n", hdr.fileoff);

    /* Show epoc6 additional header info */
    if (epocrelease == 6) {
        verbose("  installed space (last installation): %d\n", hdr.instspace);
        verbose("  max installed space: %d\n", hdr.maxinstspace);
    }

    /* Languages */
    if (langaugesSection(fp,&hdr,err,errlen)) return 1;

    /* Files */
    if (filesSection(fp,&hdr,err,errlen)) return 1;

    return 0;
}

static void guessEndianess(void)
{
    unsigned int x = 1;
    unsigned char *y = (unsigned char*) &x;

    lendian = (*y == 1);
}

enum options {OPT_HELP, OPT_EXTRACT, OPT_VERBOSE};

static struct ago_optlist optList[] = {
    {'h', "help",       OPT_HELP,       AGO_NOARG},
    {'x', "extract",    OPT_EXTRACT,    AGO_NOARG},
    {'v', "verbose",    OPT_VERBOSE,    AGO_NOARG},
    AGO_LIST_TERM
};

static struct {int opt; char *descr;} optDescr[] = {
    {OPT_HELP, "Show this help"},
    {OPT_EXTRACT, "Extract files instead to just list file names"},
    {OPT_VERBOSE, "Show more information about the SIS file(s)"},
    {0, NULL}
};

static char *getOptDescr(int opt)
{
    int i = 0;
    while (optDescr[i].descr != NULL) {
        if (optDescr[i].opt == opt)
            return optDescr[i].descr;
        i++;
    }
    return "No description available for this option";
}

static void showHelp(void)
{
    int i;

    printf("\nUsage: sisopen [options] <filename>\n");
    printf("Available options:\n");
    for (i = 0; optList[i].ao_long != NULL; i++) {
        if (optList[i].ao_short != '\0') {
            printf("  -%c ", optList[i].ao_short);
        } else {
            printf("     ");
        }
        printf("--%-13s %s",
                optList[i].ao_long,
                (optList[i].ao_flags & AGO_NEEDARG) ?
                    "<arg>" : "     ");
        printf(" | %s\n", getOptDescr(optList[i].ao_id));
    }
    printf("\nflags meaning in file listing (no verbose mode):\n"
           " f: standard file\n"
           " t: text file to show at installation time\n"
           " c: component file\n"
           " r: file to run at installation time\n"
           " x: file that will be created when the application is run\n"
           " o: file to open at installation time\n\n");
    printf("for more information please visit http://antirez.com/page/sisopen\n\n");
}

int main(int argc, char **argv)
{
    FILE *fp;
    char err[SISOPEN_ERRLEN];
    int exitcode = 0;
    char **filenames = NULL;
    int numFilenames = 0;
    int i, o;

    /* Parse command line options */
    while ((o = antigetopt(argc, argv, optList)) != AGO_EOF) {
        switch(o) {
        case AGO_UNKNOWN:
        case AGO_REQARG:
        case AGO_AMBIG:
            ago_gnu_error("sisopen", o);
            showHelp();
            exit(1);
            break;
        case OPT_HELP:
            showHelp();
            exit(1);
            break;
        case OPT_EXTRACT:
            optExtract = 1;
            break;
        case OPT_VERBOSE:
            optVerbose = 1;
            break;
        case AGO_ALONE:
            filenames = realloc(filenames,(numFilenames+1)*sizeof(char*));
            if (!filenames) {
                fprintf(stderr, "Out of memory\n");
                exit(1);
            }
            filenames[numFilenames] = ago_optarg;
            numFilenames++;
            break;
        default:
            fprintf(stderr, "Error: option currently not implemented\n");
            exit(1);
            break;
        }
    }

    if (numFilenames == 0) {
        showHelp();
        exit(1);
    }

    guessEndianess();

    for (i = 0; i < numFilenames; i++) {
        if ((fp = fopen(filenames[i], "r")) == NULL) {
            fprintf(stderr, "%s: %s opening file\n", filenames[i],
                    strerror(errno));
            exitcode = 1;
            continue;
        }
        if (sisopen(filenames[i], fp, err, SISOPEN_ERRLEN) != 0) {
            fprintf(stderr, "%s: %s\n", filenames[i], err);
            exitcode = 1;
        }
        fclose(fp);
    }
    free(filenames);
    return exitcode;
}
