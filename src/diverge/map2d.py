from __future__ import annotations

import numpy as np
from sklearn.decomposition import PCA


def project_2d(embeddings: np.ndarray) -> np.ndarray:
    embeddings = np.asarray(embeddings, dtype=np.float32)
    if len(embeddings) < 4:
        if embeddings.shape[1] < 2:
            return np.pad(embeddings, ((0, 0), (0, 2 - embeddings.shape[1])))[:, :2]
        return PCA(n_components=2, random_state=0).fit_transform(embeddings)
    try:
        import umap

        return umap.UMAP(
            n_components=2,
            n_neighbors=min(15, len(embeddings) - 1),
            random_state=0,
            transform_seed=0,
        ).fit_transform(embeddings)
    except (ImportError, ValueError):
        return PCA(n_components=2, random_state=0).fit_transform(embeddings)
