#!/usr/bin/env python3
"""Reject one-shot VideoOut renderers and unsafe changes to pinned template."""
import importlib.util
from pathlib import Path
import tempfile

root=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location(
    "interactive_videoout",root/"scripts/enable_interactive_videoout.py"
)
module=importlib.util.module_from_spec(spec)
assert spec.loader is not None
spec.loader.exec_module(module)

# Host synthetic fixture covers template patch fail-closed behavior, not GPU I/O.
fixture='''
// constexpr std::size_t frame_bytes = 0x1000000;
Canvas first{static_cast<std::uint32_t *>(mapped)};
Canvas second{reinterpret_cast<std::uint32_t *>(second_frame)};
if (sceVideoOutSubmitFlip(video, 0, 1, 1) < 0) {}
(void)sceVideoOutWaitVblank(video);
notify(ready_message);
''' + module.BEFORE
with tempfile.TemporaryDirectory() as directory:
    path=Path(directory)/"demo_renderer.cpp"
    path.write_text(fixture,encoding="utf-8")
    module.update(path)
    content=path.read_text(encoding="utf-8")
    assert "for (unsigned next_buffer = 1u;;next_buffer ^= 1u)" in content
    assert "draw(first);" in content and "draw(second);" in content
    assert "flush_range(next_frame, frame_bytes);" in content
    assert "sceVideoOutSubmitFlip(video, static_cast<std::int32_t>(next_buffer)" in content
    assert content.count("(void)sceVideoOutWaitVblank(video);")==2
    assert module.BEFORE not in content
    try:
        module.update(path)
        raise AssertionError("a double patched renderer must be rejected")
    except ValueError:
        pass
    path.write_text(fixture.replace("notify(ready_message);",""),encoding="utf-8")
    try:
        module.update(path)
        raise AssertionError("mismatched pinned runtime must be rejected")
    except ValueError:
        pass

print("Interactive native renderer patch guard: PASS")
