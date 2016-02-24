#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <err.h>
#include <errno.h>
#include <error.h>
#include <regex.h>
#include <assert.h>

void *alloc(size_t size);
void *alloc(size_t size) {
  void* m = malloc(size);
  if (!m) {
    error(1, errno, "failed to allocate memory");
  }
  return m;
}

void free2(void *d);
void free2(void *d) {
  if (d!=NULL) {
    free(d); 
  }
}


char* find_in_path(char*);

char* find_in_path(char* command) {
  char* path = getenv("PATH");
  char *match = NULL;
  if (path == NULL) {
    error(1, errno, "could not get path");
  } 
  strtok(path, ":");
  while(1) {
    char* entry = strtok(NULL, ":");
    if (entry == NULL) {
      //no more path entries
      break; 
    }
    size_t command_len = strlen(command);
    size_t entry_len = strlen(entry);
    char *fname = alloc(entry_len+command_len+2);
    strcpy(fname, entry);
    strcat(fname, "/");
    strcat(fname, command);
    struct stat sb;
    if (stat(fname, &sb) == 0 && sb.st_mode & S_IXUSR) {
      //match
      return fname;
    }
    free(fname);
  }
  return match;
}

char* get_group(const regex_t *r, const char* line, unsigned int group);
char* get_group(const regex_t *r, const char* line, unsigned int group) {
    unsigned int ngroups = group+1;
    regmatch_t *m=alloc(ngroups*sizeof(regmatch_t)); 
    char* match=NULL;
    if (regexec(r, line, ngroups, m, 0) == 0) {
      for(unsigned int i=0; i<ngroups; i++) {
        assert(m[i].rm_so != -1);
      }
      unsigned int len = (unsigned int) (m[group].rm_eo - m[group].rm_so);
      char* alias = alloc(len+1);
      memcpy(alias, line + m[group].rm_so, len);
      alias[len] = '\0';
      match = alias;
    }
    free(m);
    return match;
}

typedef struct alias_match{
  char* shell;
  char* declaration;
  char* alias_for;
} alias_match;

typedef struct type_match{
  alias_match* alias_match;
  unsigned int builtin_match;
  char pad[4];
} type_match;


void free_type_match(type_match *m);
void free_type_match(type_match *m) {
  if (m == NULL) {
    return;
  }
  if (m->alias_match != NULL) {
    alias_match* ma = m->alias_match;
    free(ma->shell);
    free(ma->declaration);
    free(ma->alias_for);
    free(ma);
  }
  free(m);
}


type_match* find_type(char*);
type_match* find_type(char* command) {
  char* shell = getenv("SHELL");
  if (shell == NULL) {
    error(1, errno, "failed to read 'SHELL' environment variable");
  }
  const char* template = "%s -ic 'type %s'";
  char* alias_command = alloc(strlen(shell) + strlen(command) + strlen(template));
  sprintf(alias_command, template, shell, command);
  FILE *fp = popen(alias_command, "r");
  if (fp == NULL) {
    error(1, errno, "failed to run alias command '%s'", alias_command);
  }
  free(alias_command);
  const char* alias_pattern = ".*is aliased to `(([^' ]*) [^']*)'";
  regex_t alias_r;
  if (regcomp(&alias_r, alias_pattern, REG_EXTENDED) != 0) {
    error(1, errno, "failed to compile regex '%s'", alias_pattern);
  }
  char* line = NULL;
  size_t rowlen = 0;
  ssize_t read;
  type_match *m = NULL;
  while ((read = getline(&line, &rowlen, fp)) != -1) {
      if(strstr(line, "is a shell builtin")) {
        m = alloc(sizeof(type_match));
        m->builtin_match=1;
        m->alias_match=NULL;
        break;
      }
      char* alias_for=get_group(&alias_r, line, 2);
      if (alias_for != NULL) {
        char* declaration=get_group(&alias_r, line, 1);
        alias_match *am = alloc(sizeof(alias_match));
        am->shell= alloc(strlen(shell)+1);
        strcpy(am->shell, shell);
        am->declaration=declaration;
        am->alias_for=alias_for;
        m = alloc(sizeof(type_match));
        m->alias_match = am;
        m->builtin_match = 0;
        break;
      }
      free(alias_for);
  }
  free(line);
  regfree(&alias_r);
  pclose(fp);
  return m;
}

typedef struct match {
  type_match *type_match;
  char* path_match;
} match;

void free_match(match* m);
void free_match(match* m) {
  if (m == NULL) {
    return;
  }
  free_type_match(m->type_match);
  free2(m->path_match);
  free(m);
}

static unsigned int FIND_TYPE=1<<0;
static unsigned int FIND_PATH=1<<1;

match *find(char* command, unsigned int bans);
match *find(char* command, unsigned int bans) {
  match* m = alloc(sizeof(match));
  m->type_match=NULL;
  m->path_match=NULL;
  if ((bans & FIND_TYPE) == 0) {
    m->type_match = find_type(command);
    if (m->type_match != NULL) {
      return m;
    }
  }
  if ((bans & FIND_PATH) == 0) {
    m->path_match = find_in_path(command);
    if (m->path_match != NULL) {
      return m;
    }
  }
  free(m);
  return NULL;
}


typedef struct cmd {
  char* name;
  struct cmd* next;
  unsigned int done;
  char buf[4];
} cmd;

int already_seen(cmd* first, char* name);
int already_seen(cmd* first, char* name) {
  cmd* current = first;
  while(current != NULL) {
    if(strcmp(name, current->name)==0 && current->done) {
      return 1;
    }
    current=current->next;
  }
  return 0;
}

cmd* mk_cmd(char* name, cmd* next);
cmd* mk_cmd(char* name, cmd* next) {
  cmd* c = alloc(sizeof(cmd));
  c->name = strdup(name);
  c->next = next;
  c->done = 0;
  return c;
}

cmd* maybe_mk_cmd(char* name, cmd* next);
cmd* maybe_mk_cmd(char* name, cmd* next) {
  if (name == NULL) {
    return NULL;
  }
  return mk_cmd(name, next);
}

void free_cmds(cmd* first);
void free_cmds(cmd* first) {
  cmd* current = first;
  while(current != NULL) {
    free(current->name);
    cmd* next = current->next;
    free(current);
    current = next;
  } 
}

void find_recursive(char* command);
void find_recursive(char* command) {
  cmd *first = mk_cmd(command, NULL);
  cmd* current = first;
  while(current != NULL) {
    printf("looking for '%s'\n", current->name);
    if (already_seen(first, current->name)) {
      printf("already searched for %s, aborting\n", current->name);
      break;
    }
    match *m = find(current->name, 0);
    if (m==NULL) {
      printf("no match\n");
      break;
    }
    char* next_name = NULL;
    if (m->type_match != NULL) {
      type_match * tm = m->type_match;
      if (tm->alias_match != NULL) {
        alias_match* am = tm->alias_match;
        printf("'%s' is an alias for '%s' in shell %s: %s\n", current->name, am->alias_for, am->shell,  am->declaration);
        next_name = am->alias_for;
      } else if(tm->builtin_match) {
        printf("'%s' is a shell builtin\n", current->name);
      }
    }
    else if (m->path_match != NULL) {
      printf("'%s' is executable %s\n", current->name, m->path_match);
      next_name = NULL;
    }
    if (next_name == NULL) {
      printf("done\n");
    }
    current->done=1;
    cmd* next = maybe_mk_cmd(next_name, NULL);
    current->next = next;
    current = next; 
    free_match(m);
  }
  free_cmds(first);
}

int main(int argc, char** argv)
{
  if (argc < 2) {
    error(1, errno, "missing command name");
  }
  char* command = argv[1];
  find_recursive(command);
  return 0;
}
