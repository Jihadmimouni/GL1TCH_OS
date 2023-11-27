# Welcome to GL1TCH OS



GL1TCH OS is an exciting project in early development aimed at creating an operating system that's both innovative and educational. This project is open for contributions from the community and is designed to be a fantastic learning opportunity for developers and enthusiasts interested in operating systems, low-level programming, and system architecture.

## Table of Contents

- [Project Overview](#project-overview)
- [Getting Involved](#getting-involved)
- [Requirments](#Requirments)
- [usage](#Steps-to-Start)
- [Contributing](#contributing)
- [Resources](#resources)

## Project Overview

GL1TCH OS is all about pushing the boundaries of what an operating system can do. It's being developed to provide a playground for experimenting with unique concepts, exploring system internals, and understanding the intricacies of OS development. Even though we're in the early stages of development, we're excited to invite you to join us on this journey.

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
