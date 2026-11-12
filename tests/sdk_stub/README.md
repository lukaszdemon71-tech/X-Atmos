# SDK stubs — for compile checking only

These are **not** the X-Plane SDK. They are the smallest set of declarations
that lets the whole plugin be type-checked on a machine with no X-Plane
installed, so a syntax error or a wrong signature is caught before you build
on Windows.

Nothing here implements anything. `tests/run.sh` compiles every source file
against these headers and links nothing; the real build uses
`-DXPLANE_SDK_PATH` pointing at the genuine SDK from
<https://developer.x-plane.com/sdk/>.

If a signature here disagrees with the real SDK, the real SDK wins and the
stub is wrong — fix the stub.
