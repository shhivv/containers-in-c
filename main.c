#define _GNU_SOURCE
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <unistd.h>

#define STACK_SIZE (1024 * 1024)
char **g_argv;
int p_stdout[2], p_stderr[2];

int run(void *arg) {
  unshare(CLONE_NEWNS);

  close(p_stdout[0]);
  close(p_stderr[0]);

  dup2(p_stdout[1], STDOUT_FILENO);
  dup2(p_stderr[1], STDERR_FILENO);

  // close redirected pipes on child end
  close(p_stdout[1]);
  close(p_stderr[1]);

  char cgroups[] = "/sys/fs/cgroup/pids/ccontainer";

  mkdir(cgroups, 0755);
  // close unused pipes on child end

  FILE *pids_max = fopen("/sys/fs/cgroup/pids/ccontainer/pids.max", "w");
  FILE *nor = fopen("/sys/fs/cgroup/pids/ccontainer/notify_on_release", "w");
  FILE *cg_procs = fopen("/sys/fs/cgroup/pids/ccontainer/cgroup.procs", "w");
  if (pids_max == NULL || nor == NULL || cg_procs == NULL) {
    perror("control group");
    exit(1);
  }

  fprintf(pids_max, "%s", "20");
  fprintf(nor, "%s", "1");
  fprintf(cg_procs, "%d", getpid());

  fclose(pids_max);
  fclose(nor);
  fclose(cg_procs);

  int sh = sethostname("child-container", strlen("child-container"));
  if (sh == -1) {
    perror("sethostname");
    exit(1);
  }

  chroot("./container_root");
  chdir("/");

  mount("proc", "proc", "proc", 0, NULL);
  execve(g_argv[2], &g_argv[2], NULL);
  perror("execve");

  umount("proc");
  exit(1);
}

int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Expected <cmd> <params>\n");
    return 1;
  }

  if (strcmp(argv[1], "run") == 0 && argv[2] != NULL) {
  } else {
    printf("bad command\n");
    return 1;
  }

  printf("Running [%s]\n", argv[2]);

  if (pipe(p_stdout) == -1 || pipe(p_stderr) == -1) {
    perror("pipe");
    return 1;
  }

  char *stack;
  char *stackTop;

  stack = mmap(NULL, STACK_SIZE, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_STACK, -1, 0);

  if (stack == MAP_FAILED) {
    perror("mmap");
    return 1;
  }

  stackTop = stack + STACK_SIZE;

  g_argv = argv;
  pid_t child_pid = clone(
      run, stackTop, CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD, NULL);

  if (child_pid == -1) {
    perror("clone");
    return 1;
  }

  // close unused pipes
  close(p_stdout[1]);
  close(p_stderr[1]);

  char o_buffer[1024];
  ssize_t o_count;
  while ((o_count = read(p_stdout[0], o_buffer, sizeof(o_buffer))) > 0) {
    write(STDOUT_FILENO, o_buffer, o_count);
  }

  char e_buffer[1024];
  ssize_t e_count;
  while ((e_count = read(p_stderr[0], e_buffer, sizeof(e_buffer))) > 0) {
    write(STDERR_FILENO, e_buffer, e_count);
  }

  return 0;
}