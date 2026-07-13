from pathlib import Path

import numpy as np

from diverge.embed import Embedder

DATA = Path(__file__).parents[1] / "data"


class CountingBackend:
    calls = 0

    def embed(self, audio: list[np.ndarray], sample_rate: int) -> np.ndarray:
        self.calls += 1
        output = np.zeros((len(audio), 512), dtype=np.float32)
        for index, signal in enumerate(audio):
            output[index, index] = float(np.mean(np.abs(signal))) + 1
            output[index, 511] = 1
        return output


def test_embedder_normalizes_batches_and_caches(tmp_path: Path) -> None:
    backend = CountingBackend()
    embedder = Embedder(cache_dir=tmp_path, backend=backend, model_id="fake")
    paths = [DATA / "loop_a.wav", DATA / "ref_a.wav"]
    first = embedder.embed_batch(paths)
    second = embedder.embed_batch(paths)
    assert first.shape == (2, 512)
    np.testing.assert_allclose(np.linalg.norm(first, axis=1), 1)
    np.testing.assert_allclose(first, second)
    assert backend.calls == 1
