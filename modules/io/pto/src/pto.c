/* SPDX-License-Identifier: BSD-3-Clause */
#include "pto_read.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <stdbool.h>

#if defined(_WIN32)
#include <io.h>
#include <process.h>
#define isatty _isatty
#define execvp _execvp
#define STDOUT_FILENO 1
#else
#include <unistd.h>
#include <sys/stat.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif
int ptoview_main(int argc, char** argv, const PtoReadFileInfo* info_arg);
#ifdef __cplusplus
}
#endif

static bool g_json = false;
static bool g_quiet = false;
static bool g_verbose = false;
static int g_color_mode = 0; /* 0=auto, 1=always, 2=never */
static bool g_force_mode = 0; /* 0=auto, 1=uid, 2=name */

static bool is_known_cmd(const char* c) {
    if (!c) return false;
    return (strcmp(c, "ls") == 0 || strcmp(c, "objects") == 0 ||
            strcmp(c, "tree") == 0 || strcmp(c, "info") == 0 ||
            strcmp(c, "cat") == 0 || strcmp(c, "extract") == 0 ||
            strcmp(c, "tags") == 0 || strcmp(c, "verify") == 0 ||
            strcmp(c, "ui") == 0 || strcmp(c, "tui") == 0 ||
            strcmp(c, "--tui") == 0 || strcmp(c, "bundle") == 0 ||
            strcmp(c, "help") == 0 || strcmp(c, "-h") == 0 ||
            strcmp(c, "--help") == 0);
}

static void print_usage(void) {
    printf(
        "PhoTon cOntainer (PTO) - Self-Executing Container (tttrlib PRD-025)\n\n"
        "Usage:\n"
        "  ./file.pto [GLOBAL...] [<command>] [ARGS...]\n\n"
        "Commands:\n"
        "  ls, objects              List objects and tags in table format (default)\n"
        "  tree                     Display low-level EBML framing & element tree\n"
        "  info                     Print comprehensive container summary & metadata\n"
        "  cat <selector>           Output object raw payload to stdout\n"
        "  extract [sel...] [-o D]  Extract object payloads to disk directory D\n"
        "  tags [selector]          List typed metadata tags for container/object\n"
        "  verify                   Validate container EBML framing & CRC checksums\n"
        "  ui, --tui                Launch interactive terminal user interface (TUI)\n"
        "  bundle <in.pto>          Create executable container bundle (-o out.pto)\n"
        "  help, -h, --help         Print this help message\n\n"
        "Globals:\n"
        "  --json                   Output machine-readable JSON format\n"
        "  --color=MODE             auto | always | never (ANSI color output)\n"
        "  -q, --quiet              Suppress non-essential output\n"
        "  -v, --verbose            Print verbose debug details\n"
        "  --uid / --name           Force selector matching by UID or name\n"
        "  --tui                    Launch interactive TUI reader mode\n"
        "  --version                Print version information\n\n"
        "Examples:\n"
        "  ./run.pto\n"
        "  ./run.pto info\n"
        "  ./run.pto --tui\n"
        "  ./run.pto cat time_trace > trace.bin\n"
        "  ./run.pto extract m000.ptu -o ./output/\n"
    );
}

static void print_version(void) {
    printf("pto reader 0.30.1 (DocTypeVersion 2, DocTypeReadVersion 1)\n");
}

static int cmd_ls(const PtoReadFileInfo* info) {
    if (g_json) {
        printf("{\"file\":\"%s\",\"title\":\"%s\",\"uuid\":\"%s\",\"generation\":%" PRIu64 ",\"objects\":[",
               info->filename, info->title, info->uuid_hex, info->generation);
        for (size_t i = 0; i < info->num_objects; i++) {
            const PtoReadObject* o = &info->objects[i];
            printf("%s{\"uid\":\"0x%08" PRIx64 "\",\"kind\":\"%s\",\"encoding\":\"%s\",\"size\":%" PRIu64 ",\"name\":\"%s\",\"rows\":%" PRIu64 ",\"offset\":%" PRIu64 ",\"aligned\":%s}",
                   (i > 0 ? "," : ""), o->uid, o->kind, o->encoding, o->size, o->name, o->rows, o->offset, o->aligned ? "true" : "false");
        }
        printf("],\"tags\":[");
        for (size_t i = 0; i < info->num_tags; i++) {
            const PtoReadTag* t = &info->tags[i];
            printf("%s{\"name\":\"%s\",\"target\":\"0x%08" PRIx64 "\",\"value\":\"%s\"}",
                   (i > 0 ? "," : ""), t->name, t->target, t->text);
        }
        printf("]}\n");
        return 0;
    }

    char size_str[32];
    pto_format_bytes(info->file_size, size_str, sizeof(size_str));

    printf("%s  \"%s\"  generation %" PRIu64 "  uuid %s  %zu objects (%s)\n\n",
           info->filename, info->title, info->generation,
           info->uuid_hex[0] ? info->uuid_hex : "none",
           info->num_objects, size_str);

    printf("%-12s %-14s %-7s %-8s NAME\n", "UID", "KIND", "ENC", "SIZE");
    for (size_t i = 0; i < info->num_objects; i++) {
        const PtoReadObject* o = &info->objects[i];
        char sz[32];
        pto_format_bytes(o->size, sz, sizeof(sz));
        printf("0x%08" PRIx64 "   %-14s %-7s %-8s %s\n",
               o->uid, o->kind[0] ? o->kind : "-", o->encoding[0] ? o->encoding : "-", sz, o->name[0] ? o->name : "-");
    }

    if (info->num_tags > 0) {
        printf("\ntags:");
        for (size_t i = 0; i < info->num_tags; i++) {
            printf("  %s=%s", info->tags[i].name, info->tags[i].text);
        }
        printf("\n");
    }

    return 0;
}

static int cmd_tree(const PtoReadFileInfo* info) {
    if (g_json) {
        printf("[");
        for (size_t i = 0; i < info->num_elements; i++) {
            const PtoReadElement* e = &info->elements[i];
            printf("%s{\"id\":\"0x%X\",\"name\":\"%s\",\"offset\":%" PRIu64 ",\"data_offset\":%" PRIu64 ",\"size\":%" PRIu64 ",\"depth\":%d}",
                   (i > 0 ? "," : ""), e->id, e->name, e->offset, e->data_offset, e->size, e->depth);
        }
        printf("]\n");
        return 0;
    }

    for (size_t i = 0; i < info->num_elements; i++) {
        const PtoReadElement* e = &info->elements[i];
        char sz[32];
        pto_format_bytes(e->size, sz, sizeof(sz));
        for (int d = 0; d < e->depth; d++) printf("  ");
        printf("%s (%X) %s @%" PRIu64, e->name, e->id, sz, e->offset);
        if (e->id == PTO_ID_FILE_DATA) {
            printf("  (%s)", (e->data_offset % 8 == 0) ? "8-byte aligned" : "unaligned");
        }
        printf("\n");
    }
    return 0;
}

static int cmd_info(const PtoReadFileInfo* info) {
    if (g_json) return cmd_ls(info);

    char sz[32];
    pto_format_bytes(info->file_size, sz, sizeof(sz));

    printf("File:            %s\n", info->filename);
    printf("Title:           %s\n", info->title[0] ? info->title : "(none)");
    printf("UUID:            %s\n", info->uuid_hex[0] ? info->uuid_hex : "(none)");
    printf("Generation:      %" PRIu64 "\n", info->generation);
    printf("DocTypeVersion:  %" PRIu64 "\n", info->doctype_version);
    printf("Objects:         %zu\n", info->num_objects);
    printf("Total Size:      %s (%" PRIu64 " bytes)\n", sz, info->file_size);
    if (info->ebml_offset > 0) {
        printf("Container Type:  Executable Bundle (P = %" PRIu64 ")\n", info->ebml_offset);
    } else {
        printf("Container Type:  Standard .pto\n");
    }
    if (info->banner[0]) {
        printf("\n--- Banner ---\n%s\n--------------\n", info->banner);
    }
    return 0;
}

static int cmd_cat(const char* file, const PtoReadFileInfo* info, const char* sel) {
    int match_count = 0;
    const PtoReadObject* obj = pto_read_find_object(info, sel, g_force_mode, &match_count);
    if (match_count == 0) {
        fprintf(stderr, "error: selector '%s' matched no object\n", sel);
        return 3;
    }
    if (match_count > 1) {
        fprintf(stderr, "error: selector '%s' matched %d objects\n", sel, match_count);
        return 2;
    }
    return pto_read_cat(file, obj, stdout);
}

static int cmd_extract(const char* file, const PtoReadFileInfo* info, int argc, char** argv) {
    const char* out_dir = ".";
    int start_idx = 0;

    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            out_dir = argv[i + 1];
            i++;
        }
    }

    /* Collect selectors */
    const PtoReadObject** targets = NULL;
    size_t num_targets = 0;

    for (int i = start_idx; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0) { i++; continue; }
        if (file && strcmp(argv[i], file) == 0) continue;
        int count = 0;
        const PtoReadObject* obj = pto_read_find_object(info, argv[i], g_force_mode, &count);
        if (count == 0) {
            fprintf(stderr, "error: selector '%s' matched no object\n", argv[i]);
            free(targets);
            return 3;
        }
        if (count > 1) {
            fprintf(stderr, "error: selector '%s' matched %d objects\n", argv[i], count);
            free(targets);
            return 2;
        }
        targets = (const PtoReadObject**)realloc(targets, sizeof(PtoReadObject*) * (num_targets + 1));
        targets[num_targets++] = obj;
    }

    if (num_targets == 0) {
        /* Extract all */
        num_targets = info->num_objects;
        targets = (const PtoReadObject**)malloc(sizeof(PtoReadObject*) * num_targets);
        for (size_t i = 0; i < num_targets; i++) targets[i] = &info->objects[i];
    }

    for (size_t i = 0; i < num_targets; i++) {
        const PtoReadObject* obj = targets[i];
        char out_path[1024];
        const char* name = obj->name[0] ? obj->name : "payload.bin";
        snprintf(out_path, sizeof(out_path), "%s/%s", out_dir, name);
        if (!g_quiet) printf("Extracting 0x%08" PRIx64 " (%s) -> %s\n", obj->uid, obj->kind, out_path);
        int res = pto_read_extract(file, obj, out_path);
        if (res != 0) {
            fprintf(stderr, "error: failed to write %s\n", out_path);
            free(targets);
            return 4;
        }
    }

    free(targets);
    return 0;
}

static int cmd_tags(const PtoReadFileInfo* info, const char* sel) {
    uint64_t target_uid = 0;
    if (sel) {
        int count = 0;
        const PtoReadObject* obj = pto_read_find_object(info, sel, g_force_mode, &count);
        if (count == 0) {
            fprintf(stderr, "error: selector '%s' matched no object\n", sel);
            return 3;
        }
        if (count > 1) {
            fprintf(stderr, "error: selector '%s' matched %d objects\n", sel, count);
            return 2;
        }
        target_uid = obj->uid;
    }

    if (g_json) {
        printf("[");
        int count = 0;
        for (size_t i = 0; i < info->num_tags; i++) {
            if (sel == NULL || info->tags[i].target == target_uid) {
                printf("%s{\"name\":\"%s\",\"target\":\"0x%08" PRIx64 "\",\"value\":\"%s\"}",
                       (count++ > 0 ? "," : ""), info->tags[i].name, info->tags[i].target, info->tags[i].text);
            }
        }
        printf("]\n");
        return 0;
    }

    for (size_t i = 0; i < info->num_tags; i++) {
        if (sel == NULL || info->tags[i].target == target_uid) {
            printf("[0x%08" PRIx64 "] %s = %s\n", info->tags[i].target, info->tags[i].name, info->tags[i].text);
        }
    }
    return 0;
}

static int cmd_verify(const char* file, const PtoReadFileInfo* info) {
    char errbuf[256] = {0};
    int res = pto_read_verify(file, info, errbuf, sizeof(errbuf));
    if (res == 0) {
        if (!g_quiet) printf("%s: VERIFIED OK (%zu objects, %zu elements)\n", file, info->num_objects, info->num_elements);
        return 0;
    } else {
        fprintf(stderr, "%s: VERIFICATION FAILED: %s\n", file, errbuf);
        return 1;
    }
}

static int cmd_bundle(const char* in_pto, const char* out_com) {
    if (!out_com) {
        out_com = in_pto;
    }

    FILE* fin = fopen(in_pto, "rb");
    if (!fin) {
        fprintf(stderr, "error: cannot open input container '%s'\n", in_pto);
        return 4;
    }

    PtoReadFileInfo check_info;
    if (pto_read_open(in_pto, &check_info) != 0) {
        fprintf(stderr, "error: '%s' is not a valid PTO container\n", in_pto);
        fclose(fin);
        return 1;
    }
    pto_read_close(&check_info);

    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    fseek(fin, 0, SEEK_SET);

    unsigned char* container_buf = (unsigned char*)malloc(file_size);
    if (!container_buf) {
        fprintf(stderr, "error: memory allocation failed\n");
        fclose(fin);
        return 1;
    }
    if (fread(container_buf, 1, file_size, fin) != (size_t)file_size) {
        fprintf(stderr, "error: failed to read input container\n");
        free(container_buf);
        fclose(fin);
        return 4;
    }
    fclose(fin);

    /* If container_buf already has a 12624 byte bundle prefix, skip prefix */
    size_t payload_offset = 0;
    if (file_size >= 12624 && ((container_buf[0] == 0x4D && container_buf[1] == 0x5A) ||
                               strncmp((char*)container_buf, ": <<", 4) == 0 ||
                               strncmp((char*)container_buf, ":;#", 3) == 0)) {
        payload_offset = 12624;
    }

    FILE* fout = fopen(out_com, "wb");
    if (!fout) {
        fprintf(stderr, "error: cannot open output file '%s'\n", out_com);
        free(container_buf);
        return 4;
    }

    /* Check if Cosmopolitan APE machine binary reader (build/pto.com) exists */
    FILE* fbin = fopen("build/pto.com", "rb");
    if (!fbin) fbin = fopen("./pto.com", "rb");
    if (!fbin) fbin = fopen("./tools/pto.com", "rb");

    if (fbin) {
        /* Embed native Cosmopolitan APE binary prefix directly */
        fseek(fbin, 0, SEEK_END);
        long bin_size = ftell(fbin);
        fseek(fbin, 0, SEEK_SET);

        unsigned char* bin_buf = (unsigned char*)malloc(bin_size);
        if (bin_buf && fread(bin_buf, 1, bin_size, fbin) == (size_t)bin_size) {
            fwrite(bin_buf, 1, bin_size, fout);
            uint64_t pad = (8 - (bin_size % 8)) % 8;
            if (pad > 0) {
                unsigned char pad_buf[8] = {0};
                fwrite(pad_buf, 1, pad, fout);
            }
            free(bin_buf);
        }
        fclose(fbin);
    } else {
        /* 12624-byte Cosmopolitan APE Polyglot Script Prefix (P = 12624, P % 8 == 0) */
        unsigned char ape_head[12618];
        memset(ape_head, ' ', sizeof(ape_head));

        ape_head[0] = 0x4D; ape_head[1] = 0x5A; ape_head[2] = 0x71; ape_head[3] = 0x46; /* MZqF */
        ape_head[4] = '='; ape_head[5] = '\''; ape_head[6] = '\n';

        /* e_lfanew pointer at 0x3C pointing to PE header at 0x80 */
        ape_head[0x3C] = 0x80; ape_head[0x3D] = 0x00; ape_head[0x3E] = 0x00; ape_head[0x3F] = 0x00;

        /* PE Signature 'PE\0\0' at 0x80 */
        ape_head[0x80] = 'P'; ape_head[0x81] = 'E'; ape_head[0x82] = 0; ape_head[0x83] = 0;
        /* IMAGE_FILE_HEADER at 0x84 (20 bytes) */
        ape_head[0x84] = 0x64; ape_head[0x85] = 0x86; /* Machine: AMD64 (0x8664) */
        ape_head[0x86] = 0x01; ape_head[0x87] = 0x00; /* NumberOfSections: 1 */
        ape_head[0x94] = 0xF0; ape_head[0x95] = 0x00; /* SizeOfOptionalHeader: 240 (0xF0) */
        ape_head[0x96] = 0x22; ape_head[0x97] = 0x00; /* Characteristics: EXECUTABLE | LARGE_ADDRESS_AWARE */

        /* IMAGE_OPTIONAL_HEADER64 at 0x98 (240 bytes) */
        ape_head[0x98] = 0x0B; ape_head[0x99] = 0x02; /* Magic: PE32+ (0x020B) */
        ape_head[0x9A] = 0x02; ape_head[0x9B] = 0x19; /* Linker Version: 2.25 */
        ape_head[0x9C] = 0x00; ape_head[0x9D] = 0x10; ape_head[0x9E] = 0x00; ape_head[0x9F] = 0x00; /* SizeOfCode: 4096 */
        ape_head[0xA0] = 0x00; ape_head[0xA1] = 0x10; ape_head[0xA2] = 0x00; ape_head[0xA3] = 0x00; /* SizeOfInitializedData: 4096 */
        ape_head[0xA8] = 0x00; ape_head[0xA9] = 0x10; ape_head[0xAA] = 0x00; ape_head[0xAB] = 0x00; /* AddressOfEntryPoint: 0x1000 */
        ape_head[0xAC] = 0x00; ape_head[0xAD] = 0x10; ape_head[0xAE] = 0x00; ape_head[0xAF] = 0x00; /* BaseOfCode: 0x1000 */
        ape_head[0xB2] = 0x40; /* ImageBase = 0x00400000 */
        ape_head[0xB8] = 0x00; ape_head[0xB9] = 0x10; ape_head[0xBA] = 0x00; ape_head[0xBB] = 0x00; /* SectionAlignment = 4096 */
        ape_head[0xBC] = 0x00; ape_head[0xBD] = 0x02; ape_head[0xBE] = 0x00; ape_head[0xBF] = 0x00; /* FileAlignment = 512 */
        ape_head[0xC0] = 0x06; ape_head[0xC8] = 0x06; /* OS / Subsystem Version 6.0 */
        ape_head[0xD0] = 0x00; ape_head[0xD1] = 0x20; ape_head[0xD2] = 0x00; ape_head[0xD3] = 0x00; /* SizeOfImage = 0x2000 */
        ape_head[0xD4] = 0x00; ape_head[0xD5] = 0x04; ape_head[0xD6] = 0x00; ape_head[0xD7] = 0x00; /* SizeOfHeaders = 0x400 */
        ape_head[0xDC] = 0x03; ape_head[0xDD] = 0x00; /* Subsystem: 3 (IMAGE_SUBSYSTEM_WINDOWS_CUI = Console App) */
        ape_head[0xE2] = 0x80; /* StackReserve = 8MB */
        ape_head[0xE9] = 0x10; /* StackCommit = 4KB */
        ape_head[0xF2] = 0x10; /* HeapReserve = 1MB */
        ape_head[0xF9] = 0x10; /* HeapCommit = 4KB */

        ape_head[0xFD] = '\''; ape_head[0xFE] = '\n';

        const char shell_script[] =
            ": << 'EOF'\n"
            "@echo off\n"
            "set \"PTO_FILE=%~f0\"\n"
            "if \"%PTO_READER%\"==\"\" where ptoview >nul 2>&1 && set \"PTO_READER=ptoview\"\n"
            "if \"%PTO_READER%\"==\"\" where pto >nul 2>&1 && set \"PTO_READER=pto\"\n"
            "if \"%PTO_READER%\"==\"\" if exist \"%~dp0ptoview.exe\" set \"PTO_READER=%~dp0ptoview.exe\"\n"
            "if \"%PTO_READER%\"==\"\" if exist \"%~dp0pto.exe\" set \"PTO_READER=%~dp0pto.exe\"\n"
            "if \"%PTO_READER%\"==\"\" if exist \"%~dp0build\\tools\\ptoview.exe\" set \"PTO_READER=%~dp0build\\tools\\ptoview.exe\"\n"
            "if \"%PTO_READER%\"==\"\" if exist \"%~dp0build\\tools\\pto.exe\" set \"PTO_READER=%~dp0build\\tools\\pto.exe\"\n"
            "if \"%PTO_READER%\"==\"\" if exist \"%~dp0build\\ptoview.exe\" set \"PTO_READER=%~dp0build\\ptoview.exe\"\n"
            "if \"%PTO_READER%\"==\"\" if exist \"%~dp0build\\pto.exe\" set \"PTO_READER=%~dp0build\\pto.exe\"\n"
            "if \"%PTO_READER%\"==\"\" set \"PTO_READER=pto\"\n"
            "\"%PTO_READER%\" \"%PTO_FILE%\" %*\n"
            "exit /b %ERRORLEVEL%\n"
            "EOF\n"
            "pD='\n'\n"
            "R=\"${PTO_READER:-}\"\n"
            "tui=0\n"
            "for a in \"$@\"; do\n"
            "  if [ \"$a\" = \"--tui\" ] || [ \"$a\" = \"ui\" ] || [ \"$a\" = \"tui\" ]; then tui=1; break; fi\n"
            "done\n"
            "if [ -z \"$R\" ]; then\n"
            "  if [ $tui -eq 1 ]; then\n"
            "    if command -v ptoview >/dev/null 2>&1; then R=ptoview\n"
            "    elif [ -x ./ptoview ]; then R=./ptoview\n"
            "    elif [ -x ./build/tools/ptoview ]; then R=./build/tools/ptoview\n"
            "    elif [ -x ./tools/ptoview ]; then R=./tools/ptoview\n"
            "    else R=ptoview; fi\n"
            "  else\n"
            "    if command -v pto >/dev/null 2>&1; then R=pto\n"
            "    elif [ -x ./pto ]; then R=./pto\n"
            "    elif [ -x ./build/tools/pto ]; then R=./build/tools/pto\n"
            "    elif [ -x ./tools/pto ]; then R=./tools/pto\n"
            "    else R=pto; fi\n"
            "  fi\n"
            "fi\n"
            "exec \"$R\" \"$0\" \"$@\"\n"
            "exit 0\n";
        memcpy(ape_head + 0x100, shell_script, strlen(shell_script));
        fwrite(ape_head, 1, sizeof(ape_head), fout);

        /* Void element (6 bytes) to bring P to 12624 (12624 % 8 == 0) */
        unsigned char void_pad[6] = {PTO_ID_VOID, 0x84, 0x00, 0x00, 0x00, 0x00};
        fwrite(void_pad, 1, sizeof(void_pad), fout);
    }

    uint64_t P = (uint64_t)ftell(fout);

    fwrite(container_buf + payload_offset, 1, file_size - payload_offset, fout);
    free(container_buf);
    fclose(fout);

#if !defined(_WIN32)
    chmod(out_com, 0755);
#endif

    if (!g_quiet) printf("Created bundle %s (payload at P=%" PRIu64 ")\n", out_com, P);
    return 0;
}

#ifdef PTO_STANDALONE_BUILD
int main(int argc, char** argv) {
    if (argc < 1) return 2;

    /* Parse global options and find command */
    int cmd_idx = -1;
    const char* file_arg = NULL;
    const char* cmd = NULL;
    const char* bundle_out = NULL;

    int i = 1;
    while (i < argc) {
        const char* arg = argv[i];
        if (strcmp(arg, "--") == 0) {
            i++;
            break;
        } else if (strcmp(arg, "--json") == 0) {
            g_json = true;
        } else if (strncmp(arg, "--color=", 8) == 0) {
            const char* val = arg + 8;
            if (strcmp(val, "always") == 0) g_color_mode = 1;
            else if (strcmp(val, "never") == 0) g_color_mode = 2;
            else g_color_mode = 0;
        } else if (strcmp(arg, "-q") == 0 || strcmp(arg, "--quiet") == 0) {
            g_quiet = true;
        } else if (strcmp(arg, "-v") == 0 || strcmp(arg, "--verbose") == 0) {
            g_verbose = true;
        } else if (strcmp(arg, "--uid") == 0) {
            g_force_mode = 1;
        } else if (strcmp(arg, "--name") == 0) {
            g_force_mode = 2;
        } else if (strcmp(arg, "--tui") == 0 || strcmp(arg, "ui") == 0 || strcmp(arg, "tui") == 0) {
            /* Recognized TUI flag */
        } else if (strcmp(arg, "-h") == 0 || strcmp(arg, "--help") == 0) {
            print_usage();
            return 0;
        } else if (strcmp(arg, "--version") == 0) {
            print_version();
            return 0;
        } else if (arg[0] == '-' && strcmp(arg, "-o") != 0) {
            fprintf(stderr, "unknown option: %s\n", arg);
            return 2;
        }
        i++;
    }

    /* Check if program is running as a bundle itself */
    PtoReadFileInfo self_info;
    bool is_bundle = (pto_read_open(argv[0], &self_info) == 0 && self_info.ebml_offset > 0);

    const char* container_file = NULL;

    /* Search for a known command among arguments */
    for (int k = 1; k < argc; k++) {
        if (strcmp(argv[k], "--tui") == 0 || strcmp(argv[k], "ui") == 0 || strcmp(argv[k], "tui") == 0) {
            cmd = "ui";
            cmd_idx = k;
            break;
        } else if (argv[k][0] != '-') {
            if (k > 1 && strcmp(argv[k - 1], "-o") == 0) continue;
            if (is_known_cmd(argv[k])) {
                cmd = argv[k];
                cmd_idx = k;
                break;
            }
        }
    }

    bool no_user_args = (argc == 1) || (is_bundle && (argc == 1 || (argc == 2 && strcmp(argv[0], argv[1]) == 0)));
    if (no_user_args) {
        print_usage();
        printf("\n");
        if (is_bundle) {
            cmd = "ls";
        } else {
            return 0;
        }
    }

    if (is_bundle) {
        container_file = argv[0];
        if (!cmd) {
            cmd = "ls";
        }
    } else {
        if (cmd) {
            /* Find container file among remaining non-option positional args */
            for (int k = argc - 1; k >= 1; k--) {
                if (k != cmd_idx && argv[k][0] != '-') {
                    if (k > 1 && strcmp(argv[k - 1], "-o") == 0) continue;
                    container_file = argv[k];
                    break;
                }
            }
        } else {
            /* No known command found. Check non-option positional args */
            int pos_count = 0;
            const char* pos_arg = NULL;
            for (int k = 1; k < argc; k++) {
                if (argv[k][0] != '-') {
                    if (k > 1 && strcmp(argv[k - 1], "-o") == 0) { k++; continue; }
                    pos_count++;
                    pos_arg = argv[k];
                }
            }
            if (pos_count == 1 && pos_arg) {
                cmd = "ls";
                container_file = pos_arg;
                print_usage();
                printf("\n");
            } else if (pos_count > 0) {
                /* First positional arg was an unknown command */
                for (int k = 1; k < argc; k++) {
                    if (argv[k][0] != '-') {
                        fprintf(stderr, "error: unknown command '%s'\n", argv[k]);
                        return 2;
                    }
                }
            }
        }
    }

    if (strcmp(cmd, "help") == 0 || strcmp(cmd, "-h") == 0 || strcmp(cmd, "--help") == 0) {
        print_usage();
        if (is_bundle) pto_read_close(&self_info);
        return 0;
    }

    if (strcmp(cmd, "bundle") == 0) {
        if (is_bundle) {
            fprintf(stderr, "error: bundle command is not available inside a bundle\n");
            if (is_bundle) pto_read_close(&self_info);
            return 2;
        }
        const char* in_pto = NULL;
        for (int k = 1; k < argc; k++) {
            if (k == cmd_idx) continue;
            if (strcmp(argv[k], "-o") == 0 && k + 1 < argc) {
                bundle_out = argv[k + 1];
                k++;
            } else if (!in_pto && argv[k][0] != '-') {
                in_pto = argv[k];
            }
        }
        if (!in_pto) {
            fprintf(stderr, "error: bundle command requires input .pto file\n");
            return 2;
        }
        return cmd_bundle(in_pto, bundle_out);
    }

    if (!container_file) {
        fprintf(stderr, "error: no container file specified\n");
        print_usage();
        if (is_bundle) pto_read_close(&self_info);
        return 2;
    }

    PtoReadFileInfo info;
    PtoReadFileInfo* infop = &info;
    if (is_bundle && strcmp(container_file, argv[0]) == 0) {
        infop = &self_info;
    } else {
        int res = pto_read_open(container_file, &info);
        if (res != 0) {
            fprintf(stderr, "error: cannot open container '%s'\n", container_file);
            if (is_bundle) pto_read_close(&self_info);
            return res;
        }
    }

    int status = 0;
    if (strcmp(cmd, "ui") == 0 || strcmp(cmd, "tui") == 0 || strcmp(cmd, "--tui") == 0) {
#if defined(PTO_HAS_TUI)
        status = ptoview_main(argc, argv, infop);
#else
        const char* reader = getenv("PTO_READER");
        if (reader && reader[0]) {
            char* exec_args[5];
            exec_args[0] = (char*)reader;
            exec_args[1] = (char*)"ui";
            exec_args[2] = (char*)container_file;
            exec_args[3] = NULL;
            execvp(reader, exec_args);
        }
        printf("Interactive TUI is not built into this reader binary.\n");
        printf("Get a reader with TUI support: https://github.com/Fluorescence-Tools/tttrlib/releases\n\n");
        status = cmd_ls(infop);
#endif
    } else if (strcmp(cmd, "ls") == 0 || strcmp(cmd, "objects") == 0) {
        status = cmd_ls(infop);
    } else if (strcmp(cmd, "tree") == 0) {
        status = cmd_tree(infop);
    } else if (strcmp(cmd, "info") == 0) {
        status = cmd_info(infop);
    } else if (strcmp(cmd, "cat") == 0) {
        const char* sel = NULL;
        if (cmd_idx + 1 < argc && strcmp(argv[cmd_idx + 1], container_file) != 0)
            sel = argv[cmd_idx + 1];
        if (!sel) {
            fprintf(stderr, "error: cat command requires a selector argument\n");
            status = 2;
        } else {
            status = cmd_cat(container_file, infop, sel);
        }
    } else if (strcmp(cmd, "extract") == 0) {
        int ext_argc = 0;
        char** ext_argv = NULL;
        if (cmd_idx + 1 < argc) {
            ext_argc = argc - (cmd_idx + 1);
            ext_argv = &argv[cmd_idx + 1];
        }
        status = cmd_extract(container_file, infop, ext_argc, ext_argv);
    } else if (strcmp(cmd, "tags") == 0) {
        const char* sel = NULL;
        if (cmd_idx + 1 < argc && strcmp(argv[cmd_idx + 1], container_file) != 0)
            sel = argv[cmd_idx + 1];
        status = cmd_tags(infop, sel);
    } else if (strcmp(cmd, "verify") == 0) {
        status = cmd_verify(container_file, infop);
    } else {
        fprintf(stderr, "error: unknown command '%s'\n", cmd);
        status = 2;
    }

    if (infop == &info) pto_read_close(&info);
    if (is_bundle) pto_read_close(&self_info);

    return status;
}
#endif
