#!/usr/bin/env python3
"""Runs the host build of q2-vortex as Q2 Pod would (fake /dev/fb0 from fbshim.so, input as text
datagrams on /tmp/q2-vortex.input.sock, TERM to end it) through one scenario, and prints what it
logged and its last state. Frames are saved as OUT_DIR/<scenario>-<name>.raw (view.py shows them).

The .ipg comes from $VX_IPG (the player's own copy; it is never part of the repository).

Usage: drive.py BUILD_DIR OUT_DIR SCENARIO
Scenarios: menus, help, play, pause, lose, finger, shots, clear"""
import os, shutil, signal, socket, subprocess, sys, tempfile, time

build, outdir, scenario = sys.argv[1:4]
build, outdir = os.path.abspath(build), os.path.abspath(outdir)
ipg = os.environ.get('VX_IPG')
if not ipg or not os.path.exists(ipg):
    sys.exit('set VX_IPG to the .ipg')
SOCK = '/tmp/q2-vortex.input.sock'
card = tempfile.mkdtemp()
app = os.path.join(card, '.vortex')
os.mkdir(app)
shutil.copy(os.path.join(build, 'q2-vortex'), app)
os.symlink(os.path.abspath(ipg), os.path.join(app, 'Vortex.ipg'))
if scenario == 'finger':
    open(os.path.join(app, 'vortex.sav'), 'w').write('control 1\nsfx 10\n')
dump = os.path.join(card, 'fb.raw')
env = dict(os.environ, LD_PRELOAD=os.path.join(build, 'fbshim.so'), FB_DUMP=dump,
           FB_FINAL=os.path.join(card, 'fb.final'), VX_STATE='1')
if scenario in ('play', 'pause', 'lose', 'clear', 'finger', 'shots'):
    env['VX_LEVEL'] = os.environ.get('VX_LEVEL_N', '1')
if scenario in ('play', 'clear', 'shots'):
    env['VX_AUTOPILOT'] = '1'
if scenario == 'shots':
    env['VX_GIVE'] = os.environ.get('VX_GIVE', '6')  # guns
if os.path.exists(SOCK):
    os.unlink(SOCK)
proc = subprocess.Popen([os.path.join(app, 'q2-vortex'), 'plughw:0,0'], env=env, cwd=app)
log = os.path.join(app, 'vortex.log')

def wait_for(cond, secs, what):
    end = time.time() + secs
    while time.time() < end:
        if cond():
            return
        time.sleep(0.05)
    proc.kill()
    sys.exit(f'FAIL: no {what} after {secs} s\n' + (open(log).read() if os.path.exists(log) else ''))

def state():
    out = {}
    try:
        for line in open(os.path.join(app, 'state.txt')):
            k, v = line.rsplit(' ', 1)
            out[k] = int(v)
    except (OSError, ValueError):
        pass
    return out

wait_for(lambda: os.path.exists(SOCK), 5, 'input socket')
wait_for(lambda: os.path.exists(os.path.join(app, 'state.txt')), 30, 'first frame')
s = socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM)

def send(m):
    s.sendto(m.encode(), SOCK)

def key(code, hold=0.06):
    send(f'k d {code}'); time.sleep(hold); send(f'k u {code}'); time.sleep(0.15)

def wheel(total, steps=10, gap=0.03):
    for _ in range(steps):
        send(f'w {total // steps}'); time.sleep(gap)

def tap(x, y):
    send(f't d {x} {y}'); time.sleep(0.05); send(f't u {x} {y}'); time.sleep(0.15)

def snap(name):
    time.sleep(0.12)
    for _ in range(50):  # fbshim rewrites the file each frame: take a whole one
        data = open(dump, 'rb').read()
        if len(data) == 320 * 375 * 4:
            break
        time.sleep(0.01)
    open(os.path.join(outdir, f'{scenario}-{name}.raw'), 'wb').write(data)

CENTRE, PLAY, PREV, NEXT, RETURN = 218, 171, 222, 223, 170
ST_MENU, ST_HELP, ST_STATS, ST_INTRO, ST_PLAY, ST_LOST, ST_CLEAR, ST_OVER = 1, 2, 3, 5, 6, 8, 9, 10
results = {}
if scenario == 'menus':
    time.sleep(0.6); snap('splash')
    key(CENTRE); time.sleep(0.4); snap('main')
    results['main'] = state()['state']
    wheel(-40, 2); time.sleep(0.4); snap('main-2')
    results['sel_after_wheel'] = state()['sel']
    key(CENTRE); time.sleep(0.4); snap('stats')
    results['stats'] = state()['state']
    key(CENTRE); time.sleep(0.2)
    wheel(-40, 2); key(CENTRE); time.sleep(0.3); snap('options')
    results['options_menu'] = state()['menu']
    c0 = state()['control']; key(CENTRE); time.sleep(0.2)
    results['control_toggled'] = int(state()['control'] != c0)
    key(CENTRE); time.sleep(0.2)  # back to how it was
    wheel(-40, 2); key(CENTRE); key(CENTRE); time.sleep(0.2)  # sfx 100 -> 0 -> 10
    results['sfx'] = state()['sfx']
    for _ in range(9):
        key(CENTRE)
    wheel(-40, 2); key(CENTRE); time.sleep(0.3); snap('soundtest')
    key(RETURN); time.sleep(0.2)
    wheel(-80, 4); key(CENTRE); time.sleep(0.3)  # back (the carousel wraps: sound test is 2 of 5)
    wheel(80, 4); time.sleep(0.2)  # to new game
    key(CENTRE); time.sleep(0.4); snap('difficulty')
    results['diff_menu'] = state()['menu']
    key(CENTRE); time.sleep(0.6); snap('intro')
    results['intro'] = state()['state']
    results['intro_level'] = state()['level']
    key(CENTRE); time.sleep(1.5); snap('playing')
    results['playing'] = state()['state']
elif scenario == 'help':
    key(CENTRE); time.sleep(0.3)
    wheel(-120, 6); key(CENTRE); time.sleep(0.4)
    results['help'] = state()['state']
    for i in range(6):
        snap(f'page{i}')
        wheel(-40, 2); time.sleep(0.3)
    results['wrapped'] = state()['help']
elif scenario == 'play':
    time.sleep(0.8); snap('intro')
    for i in range(8):
        time.sleep(1.5); snap(f't{i}')
    st = state()
    results['broken'] = st['broken']
    results['state'] = st['state']
elif scenario == 'clear':
    end = time.time() + 240
    shots = 0
    while time.time() < end:
        st = state()
        if st.get('state') == ST_CLEAR:
            time.sleep(0.5); snap('door'); time.sleep(0.8); snap('cleared')
            results['cleared'] = st['level']
            break
        if st.get('state') == ST_OVER:
            results['over_at'] = st['level']; break
        time.sleep(0.5)
        shots += 1
        if shots % 20 == 0:
            snap(f'mid{shots // 20}')
    time.sleep(1.5)
    results['next_level'] = state().get('level', 0)
    results['broken'] = state().get('broken', 0)
elif scenario == 'shots':
    time.sleep(0.5)
    for i in range(6):
        key(CENTRE, 0.04); time.sleep(0.6); snap(f'f{i}')
    results['broken'] = state()['broken']
elif scenario == 'pause':
    time.sleep(0.6)
    key(CENTRE); time.sleep(0.5)
    send(f'k d {RETURN}')
    for _ in range(7):
        time.sleep(0.07); send(f'k d {RETURN}')
    time.sleep(0.2)
    results['paused'] = state()['state']
    send(f'k u {RETURN}'); time.sleep(0.2); snap('paused')
    key(PLAY); time.sleep(0.3)
    results['resumed'] = state()['state']
    key(PLAY); time.sleep(0.3)
    wheel(-120, 6); key(CENTRE); time.sleep(0.3); snap('abandon')
    results['confirm'] = state()['menu']
    wheel(-40, 2); key(CENTRE); time.sleep(0.4)
    results['after'] = state()['state']
    results['after_menu'] = state()['menu']
elif scenario == 'lose':
    time.sleep(0.6)
    lives0 = state()['lives']
    for i in range(3):
        wheel(100, 4)  # turn the bat away from the ball
        key(CENTRE)
        end = time.time() + 25
        while time.time() < end and state().get('state') in (ST_INTRO, ST_PLAY):
            if state()['state'] == ST_INTRO:
                key(CENTRE)
            wheel(100, 2); time.sleep(0.2)
        if i == 0:
            time.sleep(0.3); snap('lost')
            results['lives_after_one'] = lives0 - state()['lives'] if state()['state'] != ST_LOST else lives0 - state()['lives'] + 1
        end = time.time() + 4
        while time.time() < end and state().get('state') == ST_LOST:
            time.sleep(0.1)
    time.sleep(0.5); snap('over')
    results['over'] = state()['state']
    key(CENTRE); time.sleep(0.4)
    results['menu_after'] = state()['state']
elif scenario == 'finger':
    time.sleep(0.6)
    a0 = state()['bat']
    send('t d 187 300'); time.sleep(0.6)
    send('t m 40 160'); time.sleep(0.6); send('t u 40 160'); time.sleep(0.3)
    results['bat'] = state()['bat']  # mrad: the touch at the view's left is PI

end = state()
proc.send_signal(signal.SIGTERM)
try:
    status = proc.wait(5)
except subprocess.TimeoutExpired:
    proc.kill()
    status = 'killed'
print(open(log).read())
print('exit status:', status)
print('panel:', open(os.path.join(card, 'fb.final')).read().strip() if os.path.exists(os.path.join(card, 'fb.final')) else '-')
print('socket left:', os.path.exists(SOCK))
for k, v in end.items():
    print(f'end: {k}: {v}')
for k, v in results.items():
    print(f'result: {k}: {v}')
shutil.rmtree(card)
