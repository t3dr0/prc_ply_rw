# Contributing to prc_ply_rw

Thanks for your interest in contributing.

## Contribution Process

1. Fork the repository and create a topic branch.
2. Make focused changes and include tests or build fixes when relevant.
3. Open a pull request against `main`.
4. Ensure required CI checks pass.
5. Address maintainer review feedback.

## Review and Merge Policy

- Contributions from non-maintainers must be submitted through pull requests.
- Pull requests require maintainer approval before merge.
- The maintainer may use direct pushes for urgent maintenance.

## Legal Requirements

By submitting a pull request, you confirm that:

1. You are legally entitled to submit the contribution.
2. The contribution is your original work, or you have rights to submit it.
3. You agree to the contributor legal terms in [CLA.md](CLA.md), including copyright assignment to
   the project owner.

Pull requests that do not accept the contributor legal terms are not eligible for merge.

The repository enforces this with a required GitHub Actions check on pull requests. Contributors
must tick the legal attestation checkbox in the pull request template.

## Coding Expectations

- Keep changes scoped and readable.
- Preserve existing style unless a file-wide reformat is intentionally part of the change.
- Add or update documentation when behaviour changes.
- Use only nanoPRC's public API (`prc_api.h`), so the tools build against any nanoPRC checkout.
- **Before opening a pull request, run the C99 check:**
  `python tests/check_c99.py --nanoprc /path/to/nanoPRC` (exit 0 = clean).
  MSVC's C mode accepts constructs C99 removed, so a green Windows build is not evidence of
  conformance.

## Security

Do not disclose security vulnerabilities in public issues. Contact the maintainer directly at
info@thames3.org.
