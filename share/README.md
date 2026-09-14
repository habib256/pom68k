# share/ — files shipped beside the executable

Guest-side binaries POM68K installs into the guest on the user's behalf.
Not private inputs (no ROM, no system media ever goes here): everything in
this directory is built from this repository's own sources and is
redistributable under the project licence.

| File | Built from | Consumer | Gate |
|---|---|---|---|
| `POM68KDisques.bin` | `dev/scsiagent/` with Retro68 (`make` → `POM68KDisques.bin`, MacBinary II) | `src/GuiAgentAutostart.h` puts it into the boot volume's Startup Items at launch; `scsi_agent_autostart_etalon` | `agent_binary_test` — decodes, and matches the Retro68 build output fork for fork when one is present |

Every package carries this directory where `MachineFactory::findPath`
looks from the executable: `usr/share/` in the AppImage and the Pi tarball,
`Contents/Resources/` in the macOS bundle, beside `POM68K.exe` in the
Windows zip. A source build finds it at the repository root.

To refresh a file after changing its sources: rebuild in `dev/<agent>/build`
(the Retro68 recipe is in each agent's README), copy the `.bin` here, and
run `agent_binary_test` — it fails when the two disagree.
