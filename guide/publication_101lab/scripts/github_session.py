"""GitHub API using EricSolshkov's existing GCM credential, never logging secrets."""
import json
import os
import subprocess
import urllib.request
import urllib.error

def token():
    p = subprocess.run(['git', 'credential', 'fill'], input='protocol=https\nhost=github.com\nusername=EricSolshkov\n\n',
        capture_output=True, text=True, timeout=30,
        env={**os.environ, 'GIT_TERMINAL_PROMPT':'0', 'GCM_INTERACTIVE':'never'})
    if p.returncode: raise RuntimeError('EricSolshkov noninteractive Git credential unavailable')
    values = dict(line.split('=',1) for line in p.stdout.splitlines() if '=' in line)
    return values['password']

def api(path, method='GET', data=None):
    req=urllib.request.Request('https://api.github.com/'+path, method=method,
        data=json.dumps(data).encode() if data is not None else None,
        headers={'Authorization':'Bearer '+token(), 'User-Agent':'101lab-publication',
                 'Accept':'application/vnd.github+json', 'Content-Type':'application/json'})
    try:
        with urllib.request.urlopen(req,timeout=60) as r:
            b=r.read()
            return json.loads(b) if b else {}
    except urllib.error.HTTPError as e:
        return {'api_error':e.code, 'detail':e.read().decode('utf8','replace')[:1000]}
