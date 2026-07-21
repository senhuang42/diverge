import numpy as np

from diverge.generator import (
    MockGenerator,
    StableAudio3Generator,
    StableAudioGenerator,
    fit_source_duration,
    transform_to_noise,
)


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


def test_short_stereo_source_is_looped_without_crossing_channels() -> None:
    source = np.asarray([[1, 2, 3], [10, 20, 30]], dtype=np.float32)
    fitted = fit_source_duration(source, 8)
    np.testing.assert_array_equal(fitted[0], [1, 2, 3, 1, 2, 3, 1, 2])
    np.testing.assert_array_equal(fitted[1], [10, 20, 30, 10, 20, 30, 10, 20])


def test_fast_generator_uses_short_sampler_and_keeps_batch_setting() -> None:
    generator = StableAudioGenerator(fast=True, batch_size=6)
    assert generator.inference_settings["steps"] == 4
    assert generator.inference_settings["batch_size"] == 6


def test_sa3_adapter_reports_truthful_capabilities() -> None:
    generator = StableAudio3Generator("small-music")
    capabilities = generator.capabilities.to_dict()
    assert capabilities["model_id"] == "stabilityai/stable-audio-3-small-music"
    assert capabilities["source_classes"] == ("music",)
    assert capabilities["audio_to_audio"] is True
    assert capabilities["audio_direction"] is False
    assert capabilities["inpainting"] is True
    assert capabilities["latency_status"] == "unmeasured"
    assert capabilities["protocol_version"] == 1
    assert capabilities["upstream_revision"] == "124e8a799f57a1f665495ecb72e547d0a62867f1"
    assert capabilities["redistribution_review_required"] is True


def test_sa3_adapter_batches_audio_to_audio_with_exact_duration() -> None:
    import torch

    calls = []

    class FakeModel:
        def generate(self, **kwargs):
            calls.append(kwargs)
            return torch.ones((kwargs["batch_size"], 2, round(kwargs["duration"] * 44_100)))

    generator = StableAudio3Generator(
        "small-sfx", batch_size=2, model_factory=lambda *_args, **_kwargs: FakeModel()
    )
    source = np.zeros((2, 11_025), dtype=np.float32)
    output = generator.generate(source, 44_100, np.zeros(4), "metal impact", 40, 0.25, 7, 3)
    assert [item.shape for item in output] == [(2, 11_025)] * 3
    assert [call["batch_size"] for call in calls] == [2, 1]
    assert [call["seed"] for call in calls] == [7, 9]
    assert all(call["init_audio"][0] == 44_100 for call in calls)
    assert all(call["prompt"] == "metal impact" for call in calls)
