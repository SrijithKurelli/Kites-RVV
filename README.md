# KITES : RISC - V SIMULATOR

Kites is a RISC-V simulator and assembly code editor. This project branch extends
the simulator with a selected RISC-V Vector (RVV) instruction subset in the
single-cycle RVSS processor and all four five-stage pipeline variants. The
implemented vector state uses VLEN=128, LMUL=1, and SEW=8/16/32/64; this is an
incremental subset, not full RVV 1.0 compliance. See
[the vector extension notes](docs/VECTOR_EXTENSION.md).

## Downloading and Installation

Prebuilt libraries are available for Linux,Windows on [Release Page](https://github.com/hghit-harshit/Kites/releases)

### Ubuntu(for only ubuntu-22.04 and above)
- Run chmod +x for the Kites AppImage.
- Run the file.

### Windows
- All the required files are inside the zip file, so you can just run the file.
- In case you get popup regarding some missing dll please contact us.

## MacOS
- Put Kites in the application 

RUN

```bash
sudo xattr -rd com.apple.quarantine sudo xattr -rd com.apple.quarantine /path/to/application.app

```

## Building
To build Kites you will first need to install Qt(version above 6.9)
Then simply build using

```bash
cmake -DCMAKE_PREFIX_PATH="path/to/Qt/6.x.x/<compiler>" ..
```

## Testing

See [docs/TESTING.md](docs/TESTING.md) for how to build and run tests, including coverage reports.

## CLI

See [docs/CLI.md](docs/CLI.md) for how to build and use the headless command-line executable. For the CS2323 walkthrough, example commands, expected results, and final verification checklist, see [docs/DEMO_GUIDE.md](docs/DEMO_GUIDE.md).
