import json
import os
from pathlib import Path

import pytest

from canbench.bench import NodeBench


def selected_backends():
    path = Path('artifacts/backend.json')
    if path.exists() and json.loads(path.read_text())['backend'] == 'socketcan':
        return ['memory', 'socketcan']
    return ['memory']


@pytest.fixture(params=selected_backends())
def bench(request):
    executable = Path(os.environ.get('CAN_NODE', 'build/can_node'))
    node = NodeBench(executable, request.param)
    yield node
    node.save(Path('artifacts/bench') / f'{request.node.originalname}.{request.param}.log')
    node.close()
