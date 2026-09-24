#include <stdlib.h>
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <stdbool.h>

typedef struct ext_info {
  char ext[24];
  size_t files, size_min, size_max, size_total;
} ext_info;

typedef struct trie {
  struct trie *nodes;
  size_t nodes_capacity;
  size_t nodes_size;

  char ch;
  size_t files; // number of files of this type
  // min and max file size and total file size
  size_t size_min, size_max, size_total;
} trie;

// add file to the trie
void add(trie *at, char *suffix, size_t size) {
  char ch = *suffix;
  suffix++;

  // get or append ch node from at
  trie *found = NULL;
find:
  if (ch == 0) {
    found = at;
    suffix = "";
  } else {
    // FIXME: sorted + bsearch (but it's plenty fast enough as is)
    for (size_t i = 0; i < at->nodes_size; i++) {
      if (at->nodes[i].ch == ch) {
        found = &at->nodes[i];
        break;
      }
    }
  }
  if (!found) {
    if (at->nodes_size == at->nodes_capacity) {
      size_t new_capacity = at->nodes_capacity ? at->nodes_capacity * 2 : 8;
      at->nodes = realloc(at->nodes, sizeof(trie) * new_capacity);
      at->nodes_capacity = new_capacity;
    }
    at->nodes[at->nodes_size++] = (trie){.ch = ch};
    goto find;
  }

  if (*suffix != 0) {
    add(found, suffix, size);
  } else {
    if (!found->files) {
      found->size_min = size;
      found->size_max = size;
    } else {
      found->size_min = found->size_min < size ? found->size_min : size;
      found->size_max = found->size_max > size ? found->size_max : size;
    }
    found->size_total += size;
    found->files++;
  }
}

void to_array(trie *at, char *prefix, ext_info **arr, size_t *count) {
  char suffix[24];
  snprintf(suffix, 24, "%s%c", prefix, at->ch);
  if (at->files) {
    **arr = (ext_info){.files = at->files,
                       .size_min = at->size_min,
                       .size_max = at->size_max,
                       .size_total = at->size_total};
    ext_info *e = *arr;
    memcpy(e->ext, suffix, 24);
    *arr = e+1;
    *count += 1;
  }
  for (int i = 0; i < at->nodes_size; i++) {
    to_array(&at->nodes[i], suffix, arr, count);
  }
}

const char *spinner[] = {"▁","▃","▄","▅","▆","▇","█","▇","▆","▅","▄","▃"};


void recurse(trie *root, const char *path) {
  DIR *dir;
  struct dirent *entry;
  char buf[1024];
  clock_t now = clock();

  static int count = 0;
  count += 1;

  printf("\e[0K%s (%d) Spelunking in: %s\r", spinner[(now/(CLOCKS_PER_SEC/15))%12], count, path);

  dir = opendir(path);
  while ((entry = readdir(dir))) {
    snprintf(buf, 1024, "%s/%s", path, entry->d_name);
    if (entry->d_type == DT_REG) {
      char *suffix = strrchr(entry->d_name, '.');
      if (suffix) {
        suffix++; // skip the dot
      } else {
        suffix = "";
      }
      struct stat st;
      stat(buf, &st);
      //printf("file %s has size %lld\n", entry->d_name, st.st_size);
      add(root, suffix, st.st_size);
    } else if (entry->d_type == DT_DIR) {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
        continue;
      recurse(root, (const char*)buf);
    }
  }
  closedir(dir);
}

#define COMPARE_FN(field)                                                      \
  int cmp_##field(const void *aptr, const void *bptr) {                        \
    ext_info *a = (ext_info *)aptr;                                            \
    ext_info *b = (ext_info *)bptr;                                            \
    if (a->field < b->field)                                                   \
      return -1;                                                               \
    else if (a->field > b->field)                                              \
      return 1;                                                                \
    else                                                                       \
      return 0;                                                                \
  }

COMPARE_FN(size_total);
COMPARE_FN(files);
COMPARE_FN(size_min);
COMPARE_FN(size_max);

typedef int (*Comparator)(const void*, const void*);

void format_size(size_t sz) {
  if (sz >= (1 << 20)) {
    printf("%11.1fM", sz/(float)(1<<20));
  } else if (sz >= (1 << 10)) {
    printf("%11.1fK", sz/(float)(1<<10));
  } else {
    printf("%11zuB", sz);
  }
}

bool parse_args(int argc, char **argv, long *top_n, Comparator *cmp) {
  char **arg = argv;
  argc--;
  arg++; // skip executable name

  while (argc) {
    if (strcmp(arg[0], "-s") == 0) {
      if (argc < 2) goto parameter_required;
      if (strcmp(arg[1], "files") == 0) {
        *cmp = &cmp_files;
      } else if (strcmp(arg[1], "max") == 0) {
        *cmp = &cmp_size_max;
      } else if (strcmp(arg[1], "min") == 0) {
        *cmp = &cmp_size_min;
      } else if (strcmp(arg[1], "total") == 0) {
        *cmp = &cmp_size_total;
      } else {
        goto unknown_parameter;
      }
      argc -= 2;
      arg += 2;
    } else if (strcmp(arg[0], "-n") == 0) {
      if (argc < 2) goto parameter_required;
      *top_n = atol(arg[1]);
      argc -= 2;
      arg += 2;
    } else {
      goto unknown_flag;
    }

  }
  return true;
unknown_flag:
  fprintf(stderr, "unknown flag: %s\n", arg[0]);
  return false;
parameter_required:
  fprintf(stderr, "%s: parameter required\n", arg[0]);
  return false;
unknown_parameter:
  fprintf(stderr, "%s: unrecognized value %s\n", arg[0], arg[1]);
  return false;
}

int main(int argc, char **argv) {
  long top_n = 99999999;
  Comparator cmp = &cmp_size_total;
  if (!parse_args(argc, argv, &top_n, &cmp))
    return 1;

  trie root = {0};
  recurse(&root, ".");

  ext_info *arr =
      malloc(sizeof(ext_info) * 1024); // should be enough for everybody!
  size_t count = 0;



  ext_info *to = arr;
  to_array(&root, "", &to, &count);
  printf("  File extension |     min size |     max size |     tot size |  files\n");
  printf("  ---------------+--------------+--------------+--------------+-------\n");
  qsort(arr, count, sizeof(ext_info), cmp);
  size_t total = 0, files = 0;
  for (int i = 0; i < count; i++) {
    //printf(" printing %d / %zu\n", i, count);
    ext_info t = arr[i];
    printf("%16s | ",t.ext[0] == 0 ? "(none)" : t.ext);
    format_size(t.size_min);
    printf(" | ");
    format_size(t.size_max);
    printf(" | ");
    format_size(t.size_total);
    printf(" | ");
    printf("%6zu\n", t.files);
    total += t.size_total;
    files += t.files;
  }
  printf("  ------------------------------------------------------------+-------\n%42sTOTAL: ", "");
  format_size(total);
  printf(" | %6zu\n", files);
}
