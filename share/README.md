# share/ — files shipped beside the executable

Guest-side binaries POM68K installs into the guest on the user's behalf.
Not private inputs (no ROM, no system media ever goes here): everything in
this directory is built from this repository's own sources and is
redistributable under the project licence.

| File | Built from | Consumer | Gate |
|---|---|---|---|
| `POM68KDisques.bin` | `dev/scsiagent/` with Retro68 (`make` → `POM68KDisques.bin`, MacBinary II) | `src/GuiAgentAutostart.h` puts it into the boot volume's Startup Items at launch; `scsi_agent_autostart_etalon` | `agent_binary_test` — decodes, and matches the Retro68 build output fork for fork when one is present and was built by the reference toolchain |

Every package carries this directory where `MachineFactory::findPath`
looks from the executable: `usr/share/` in the AppImage and the Pi tarball,
`Contents/Resources/` in the macOS bundle, beside `POM68K.exe` in the
Windows zip. A source build finds it at the repository root.

To refresh a file after changing its sources: rebuild in `dev/<agent>/build`
(the Retro68 recipe is in each agent's README), copy the `.bin` here
together with the build's `POM68KDisques.toolchain`, and run
`agent_binary_test` — it fails when the two disagree.

**The reference toolchain.** The same sources give different code under
another Retro68 or GCC: Retro68 `f99ecb5` with GCC 16.1.0 emits about
23 000 bytes of resource fork that differ from the shipped copy
(`CHANGELOG` 2026-10-14 (seventh)). `POM68KDisques.toolchain` names the
toolchain that built the shipped `.bin`; the agent's CMake writes the same
one-line stamp (`Retro68 <commit> GCC <version>`) beside its output. The
gate compares the forks only when the two lines are equal, and otherwise
holds the build to the application's identity and prints that it did not
compare them. The copy shipped on 2026-09-14 predates the stamp, so its
line reads `unrecorded` and no build compares against it until a refresh
records one. A different toolchain is proven by the agent etalons
(`*_agent_boot_etalon`, `scsi_agent_*`), which load the local build first.
