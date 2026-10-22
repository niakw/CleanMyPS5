#!/usr/bin/env python3
"""Convert the pinned PS5 demo's one-frame screen into an interactive UI loop.

Only operates on the temporary checkout of the exact pinned boilerplate.
Never edits or distributes the upstream repo or its build artifacts.
"""
from pathlib import Path
import sys

BEFORE = """    // Returning from main or calling exit crashes this launch context.
    for (;;)
        (void)sceKernelUsleep(1000000);
}"""
AFTER = """    // Returning from main or calling exit crashes this launch context.
    // CleanMyPS5's DrawScene reads the DualSense and renders a new frame.
    // Always draw into the BACK buffer, never into the buffer on screen.
    // The first buffer (index 0) was submitted above; index 1 is next.
    for (unsigned next_buffer = 1u;;next_buffer ^= 1u)
    {
        void *next_frame = next_buffer != 0u ? second_frame : mapped;
        if (next_buffer != 0u)
            draw(second);
        else
            draw(first);
        flush_range(next_frame, frame_bytes);
        if (sceVideoOutSubmitFlip(video, static_cast<std::int32_t>(next_buffer), 1, 1) < 0)
            halt("CleanMyPS5: native frame presentation failed");
        (void)sceVideoOutWaitVblank(video);
    }
}"""

def update(path: Path) -> None:
    source=path.read_text(encoding="utf-8")
    # Reject unexpected versions, incorrect VideoOut ordering, or double patch.
    anchors=(
        "constexpr std::size_t frame_bytes = 0x1000000;",
        "Canvas first{static_cast<std::uint32_t *>(mapped)};",
        "Canvas second{reinterpret_cast<std::uint32_t *>(second_frame)};",
        "if (sceVideoOutSubmitFlip(video, 0, 1, 1) < 0)",
        "(void)sceVideoOutWaitVblank(video);",
        "notify(ready_message);",
    )
    if source.count(BEFORE)!=1 or any(source.count(a)!=1 for a in anchors):
        raise ValueError("unexpected pinned PS5 VideoOut runtime; refusing to patch")
    path.write_text(source.replace(BEFORE,AFTER),encoding="utf-8")
    print("PS5 VideoOut native interactive double-buffer presentation: enabled")

if __name__=="__main__":
    if len(sys.argv)!=2:raise SystemExit("usage: enable_interactive_videoout.py SRC")
    try: update(Path(sys.argv[1]))
    except (OSError,ValueError) as exc:raise SystemExit(f"Renderer patch error: {exc}") from exc
