#!/usr/bin/env python3
"""Preserve command status and publish bounded diagnostics in the CI summary."""
import argparse
import collections
import os
import re
import subprocess
import sys
from pathlib import Path

parser=argparse.ArgumentParser()
parser.add_argument('label')
parser.add_argument('command',nargs=argparse.REMAINDER)
args=parser.parse_args()
command=args.command[1:] if args.command[:1]==['--'] else args.command
if not command:parser.error('missing command')
sys.stdout.reconfigure(encoding='utf-8',errors='replace')
log=Path('ToneDock/build/ci-logs')/(re.sub(r'[^a-z0-9]+','-',args.label.lower()).strip('-')+'.log')
log.parent.mkdir(parents=True,exist_ok=True)
tail=collections.deque(maxlen=100)
with log.open('w',encoding='utf-8') as output:
    process=subprocess.Popen(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,encoding='utf-8',errors='replace')
    for line in process.stdout:
        sys.stdout.write(line)
        output.write(line)
        tail.append(line)
    result=process.wait()
summary=os.environ.get('GITHUB_STEP_SUMMARY')
if summary:
    with Path(summary).open('a',encoding='utf-8') as output:
        output.write(f'## {args.label}\n\nExit code: **{result}**\n\n```text\n')
        output.write(''.join(tail)[-24000:].replace('```',"'''"))
        output.write('\n```\n')
if result:
    # Annotations are visible in the public run overview even when detailed
    # logs or step summaries require a GitHub login.
    def escape(value):
        return value.replace('%', '%25').replace('\r', '%0D').replace('\n', '%0A')
    title = escape(args.label).replace(':', '%3A').replace(',', '%2C')
    detail = escape(f'Exit code: {result}\n' + ''.join(tail)[-6000:])
    print(f'::error title={title}::{detail}', flush=True)
sys.exit(result)
