# test_tas_lifecycle_eval_mutation_part2.ps1
# TEST-TAS-LIFE-MUT, part 2 of 2: mutants 55 to 107 of the audit in test_tas_lifecycle_eval_mutation.ps1, which holds the
# operators, the verdict rules and the negative controls.  Split in two mandatory Unit entries so each stays inside the
# unchanged 60 s Unit timeout (Q, 2026-11-10); part 1 is the other entry.  Offline logic only, never HIL evidence.
# Traces to: #364.

& (Join-Path $PSScriptRoot 'test_tas_lifecycle_eval_mutation.ps1') -Part 2
exit $LASTEXITCODE
