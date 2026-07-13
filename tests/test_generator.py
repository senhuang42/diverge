import numpy as np

from diverge.generator import MockGenerator, transform_to_noise


def test_transform_mapping_is_monotonic() -> None:
    assert transform_to_noise(0) == 0.1
    assert transform_to_noise(100) == 1.0
    assert transform_to_noise(10) < transform_to_noise(50) < transform_to_noise(90)


def test_mock_generator_is_deterministic_and_audible() -> None:
    source = np.ones((2, 4_410), dtype=np.float32) * 0.1
    embedding = np.zeros(512, dtype=np.float32)
    generator = MockGenerator()
    first = generator.generate(source, 44_100, embedding, "test", 50, 0.1, 7, 2)
    second = generator.generate(source, 44_100, embedding, "test", 50, 0.1, 7, 2)
    assert len(first) == 2
    np.testing.assert_array_equal(first, second)
    assert all(np.sqrt(np.mean(item**2)) > 0.01 for item in first)
