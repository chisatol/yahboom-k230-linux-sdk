#!/usr/bin/python3
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time

directory = '/run/xiaozhi-enroll'
os.makedirs(directory, mode=0o700, exist_ok=True)
if sys.argv[1] == 'record':
    index = int(sys.argv[2])
    if index not in (1, 2, 3):
        sys.exit(2)
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind(('127.0.0.1', 5682))
    sock.settimeout(2)
    end = time.monotonic() + 4
    with open(f'{directory}/{index}.pcm', 'wb') as out:
        while time.monotonic() < end:
            out.write(sock.recv(8192))
elif sys.argv[1] == 'finish':
    candidate = f'{directory}/keyword.json'
    result = subprocess.run(['/opt/xiaozhi/xiaozhi-open-wake',
                             '/opt/xiaozhi/open-kws/kws.kmodel',
                             '/opt/xiaozhi/open-kws/assets', candidate,
                             *(f'{directory}/{i}.pcm' for i in (1, 2, 3))])
    if result.returncode:
        sys.exit(result.returncode)
    destination = '/home/elaina/.config/xiaozhi-wake.json'
    with tempfile.NamedTemporaryFile(dir=os.path.dirname(destination), delete=False) as out:
        temporary = out.name
        with open(candidate, 'rb') as source:
            shutil.copyfileobj(source, out)
        out.flush()
        os.fsync(out.fileno())
    os.chmod(temporary, 0o644)
    os.replace(temporary, destination)
    shutil.rmtree(directory)
    os.unlink('/run/xiaozhi-enroll-mode')
else:
    sys.exit(2)
