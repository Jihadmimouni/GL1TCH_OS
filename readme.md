# Welcome to GL1TCH OS



GL1TCH OS is an exciting project in early development aimed at creating an operating system that's both innovative and educational. This project is open for contributions from the community and is designed to be a fantastic learning opportunity for developers and enthusiasts interested in operating systems, low-level programming, and system architecture.

## Table of Contents

- [Project Overview](#project-overview)
- [What's left for a minimally functional OS](#whats-left-for-a-minimally-functional-os)
- [Getting Involved](#getting-involved)
- [Requirments](#Requirements)
- [usage](#Steps-to-Start)
- [Docker](#docker)
- [Contributing](#contributing)
- [Resources](#resources)

## Project Overview

GL1TCH OS is all about pushing the boundaries of what an operating system can do. It's being developed to provide a playground for experimenting with unique concepts, exploring system internals, and understanding the intricacies of OS development. Even though we're in the early stages of development, we're excited to invite you to join us on this journey.

## What's left for a minimally functional OS

Right now GL1TCH OS can boot, read/write a FAT12 filesystem, and run one
built-in interactive shell with a handful of commands - but under the hood
it's still a single monolithic real-mode program, not yet a real OS with
the usual separation between kernel and programs. Rough checklist of what's
actually missing before it's reasonably "minimally functional" (based on
reading the current source, not a generic OS wishlist) - check items off as
they land:

**Architectural (blocking - everything else tends to need these first):**
- [ ] **Memory manager.** There's no heap/allocator anywhere; every buffer in
  the kernel is a fixed-size stack or static array. Needed before anything
  below can deal with data whose size isn't known at compile time.
- [ ] **Program loading.** There's no way to load and run a separate binary
  from disk - every shell command is compiled straight into `kernel.bin`.
  (`tools/hello.c`'s `hello1.bin`, copied onto the floppy as `::fat`, looks
  like a leftover test artifact rather than an actual loader path.)
- [ ] **Interrupt-driven timer and keyboard.** `read_line` blocks on a BIOS
  `int 16h` poll; there's no IRQ0 timer tick and no custom IDT, so there's no
  `sleep`, no preemption, and nothing can happen while waiting on a keypress.
- [ ] **Multitasking**, even cooperative (no preemption needed yet) - right
  now exactly one thing ever runs: the shell loop.
- [ ] **Fault/panic handling.** Real mode has no hardware memory protection,
  so a bad pointer just silently corrupts memory; there's no panic screen or
  diagnostic dump, just `kmain`'s plain `cli; hlt` as the last resort.
- [ ] **Protected mode / memory beyond 1MB.** Still pure 16-bit real mode, so
  usable RAM is capped under 1MB no matter how much is actually installed.
- [ ] **Booting from more than a 1.44MB floppy at LBA 0.** The FAT driver
  assumes the boot drive *is* the whole filesystem, with no MBR/partition
  table support, so it can't boot off a hard disk or USB image.

**Smaller / non-blocking polish:**
- [ ] `cp` / `mv` / `df`-equivalent shell commands.
- [ ] Long filenames (currently 8.3 only) and real timestamps on
  `mkdir`/`touch`/`write` - new FAT entries are written with zeroed dates.
- [ ] Shell scripting - no `&&`, `;`, piping, or redirection; one command per
  line only.
- [x] Clean up `src/bootloader/stage2/main.c`'s empty `cstart_` function -
  looks unused.

## Getting Involved

We believe in the power of open collaboration. If you're passionate about operating systems, programming, or just curious to learn, there are several ways you can get involved:

- **Fork:** Start by forking this repository to your own GitHub account.
- **Clone:** Clone the forked repository to your local machine.
- **Explore:** Dive into the code, documentation, and issues to understand the project better.
- **Contribute:** Contribute by fixing bugs, adding features, or enhancing documentation.
- **Discuss:** Join discussions on GitHub issues to share your ideas and feedback.
- **Spread the Word:** Share the project with others who might be interested.

## Requirements:
- **Linux OS:** All scripts are written in Bash, so a Linux environment is required.
- **Qemu:** Virtualization software. Download from [Qemu website](https://www.qemu.org/download/).
- **NASM:** Assembly compiler. Install from [NASM website](https://www.nasm.us/).
- **mtools:** Tool for manipulating the floppy disk (used in the early stages). Install from [mtools website](https://www.gnu.org/software/mtools/).
- **make:** Build automation tool. Install using your package manager (e.g., `sudo apt-get install make`).
- **Bochs:** Debugger and emulator. Optional for debugging purposes. Install from [Bochs website](https://bochs.sourceforge.io/).
- **File Editor:** Any text editor of your choice for code modifications.
- **WCC** (Watcom C Compiler): C compiler for compiling the kernel as it supports the 16-bit mode. Install from [WCC repository](https://github.com/open-watcom/open-watcom-v2).

## Steps to Start:
1. **Install Requirements:**
   - Ensure that you have a Linux environment.
   - Install Qemu, NASM, mtools, make, and Bochs using the provided links or your package manager.

2. **Build the Project:**
   - Run the `make` command in the project directory to build the project.

3. **Set Execution Permissions:**
   - Execute `chmod +x run.sh` to grant execution permissions to the `run.sh` script.

4. **Execute the Project:**
   - Run the project using `./run.sh`.

5. **Debug with Bochs (Optional):**
- If you want to debug the project using Bochs, run the appropriate Bochs command with your configuration. For example:
  `
  bochs -f bochsrc.txt
  `
  or by just using the default config and runing `./debug.sh` 
                                                         
                                                     
- Note: Bochs is optional and can be used for debugging purposes. If not needed, you can skip this step.


## Docker

No local toolchain? Build and run GL1TCH OS with just Docker - it installs NASM, Open Watcom, mtools/dosfstools and builds `build/main_floppy.img` inside the container, so the build is reproducible on any machine that has Docker.

1. **Build the image:**
   ```
   docker build -t gl1tch-os .
   ```

2. **Run it** (boots straight into QEMU, with the OS's text output shown right in your terminal via QEMU's curses display):
   ```
   docker run --rm -it gl1tch-os
   ```

3. **Extract the built floppy image to `build/main_floppy.img`** so `./run.sh` (or Bochs, or your own `qemu-system-i386` invocation) boots what you just built. **Note:** `docker build` only produces the `gl1tch-os` image - it never touches your host `build/` directory on its own, so skipping this step means `run.sh` keeps booting whatever was there before:
   ```
   id=$(docker create gl1tch-os)
   docker cp "$id":/os/main_floppy.img ./build/main_floppy.img
   docker rm "$id"
   ```
   Or just run `./docker-build.sh`, which does steps 1 and 3 in one go.

## Contributing

We welcome contributions from developers of all skill levels. Whether you're an experienced developer or just starting, your ideas and contributions are valuable to us. To contribute, follow these steps:

1. Check the [Issues](https://github.com/Jihadmimouni/GL1TCH_OS/issues) section for tasks and bugs you can work on.
2. Fork the repository and create a new branch for your contribution.
3. Make your changes, following the coding guidelines and best practices.
4. Test your changes thoroughly.
5. Submit a pull request, explaining the changes you've made and why they should be merged.

## Resources

To kickstart your journey with GL1TCH OS development, we recommend checking out the following resources:

- [YouTube Playlist](https://www.youtube.com/playlist?list=PLFjM7v6KGMpiH2G-kT781ByCNC_0pKpPN): This playlist is a cornerstone of our project, providing valuable insights into OS development.
- [Documentation(still not created)](link_to_documentation): Our documentation hub where you can find guides, tutorials, and more.

Let's come together and create something truly exceptional. Join us in shaping the future of GL1TCH OS!

---

**Note:** GL1TCH OS is currently in early development. Features, design, and functionality are subject to change as the project progresses.
