# research/

Tools used to work out the save format. Not needed to run CounterChao. Run from the prototype folder, e.g.
`python research/save_lab.py init`. They write to `lab/` (gitignored). Game must be closed for anything that
edits save files; the memory tools need the game open.

| Tool | Purpose |
|------|---------|
| save_lab.py | snapshot / diff / restore save files with a log |
| diff_saves.py | byte diff of two files |
| poke.py | edit a closed save file (S01 checksum auto-adjusted) |
| find_base.py | locate the Chao image in game memory (read-only) |
| mem_dump.py | view bytes of the live image (read-only) |
| mem_poke.py | write bytes into the live image |
| find_alf_checksum.py, analyze_alf_field.py | failed attempts to identify the ALF checksum |
| explore_data.py, fetch_last_match.py | Leetify stat exploration |
| RUNBOOK.md | clean-slate procedure used for the experiments |
