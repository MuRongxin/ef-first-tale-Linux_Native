"""Exercise the default title, every chapter, session disposal and title loading in Xvfb."""
import argparse
import ctypes as C
import hashlib
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import time
from PIL import Image, ImageGrab
from message_ui_smoke import ROOT, prepare_fixture
from script_json import write_fixture_scripts


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary',type=Path,default=ROOT/'build/dev/ef_native')
    parser.add_argument('--out',type=Path,default=ROOT/'build/title-flow-smoke')
    parser.add_argument('--real-content',type=Path,help='Local acceptance only; uses the installed package without writing to it')
    parser.add_argument('--audio',action='store_true',help='Exercise real SDL callback lifecycle with the dummy audio device')
    parser.add_argument('--movie-return',action='store_true',help='Exercise cancellation and return during an unskippable movie (FFmpeg build)')
    parser.add_argument('--wm',default=shutil.which('openbox'),help='Optional window manager for maximize/fullscreen acceptance')
    parser.add_argument('--gallery-only',action='store_true',help='Exercise the title/gallery/viewer loop and async cache')
    parser.add_argument('--title-only',action='store_true',help='Check original title atlas cropping, hotzones and settings')
    a=parser.parse_args();out=a.out;out.mkdir(parents=True,exist_ok=True)
    if a.movie_return and a.real_content:parser.error('--movie-return uses synthetic content only')
    entries=['000_01mugai','100_01','700_01mugai','200_01']
    if not a.real_content:
        prepare_fixture(out)
        manifest=json.loads((out/'manifest.json').read_text())
        for name,size in [('saveloadBase',(800,600)),('saveloadBase0',(800,600)),('saveloadSelect',(344,98)),('notsaved',(100,75))]:
            Image.new('RGBA',size,(20,35,70,255)).save(out/(name+'.png'))
            manifest['images']['sys/'+name+'.png']={'design_size':list(size),'variants':{'1':{'path':name+'.png','pixels':list(size)}}}
        # Title atlas fixtures use distinct base/hover pixels to detect accidental full-image overlays.
        for name,color in [('topmenu2',(72,100,130,255)),('topmenu2Over',(230,180,70,255))]:
            Image.new('RGBA',(800,600),color).save(out/(name+'.png'))
            manifest['images']['sys/'+name+'.png']={'design_size':[800,600],'variants':{'1':{'path':name+'.png','pixels':[800,600]}}}
        shutil.copyfile(ROOT/'examples/title.ui.json',out/'title.ui.json')
        if a.gallery_only:
            for n in range(1,15):
                for suffix in ['a01','b01']:
                    for tier in [4,8]:
                        path=f'cg-{n}-{suffix}-{tier}.webp'
                        color=(40+n*10,80 if suffix=='a01' else 170,210,255)
                        Image.new('RGBA',(80*tier,60*tier),color).save(out/path,lossless=True)
                    manifest['images'][f'bg/ev01_{n:03d}{suffix}.png']={'design_size':[80,60], 'variants':{
                        str(t):{'path':f'cg-{n}-{suffix}-{t}.webp','pixels':[80*t,60*t]} for t in [4,8]}}
            # Sky/room/Cutin files are absent deliberately: catalog must exclude them.
            for name in ['ev01_sky_a_sepia','ev01_033a01','ev01_013_kiridashi_base']:
                manifest['images']['bg/'+name+'.png']={'design_size':[800,600],'variants':{'1':{'path':'missing.png','pixels':[800,600]}}}
        (out/'manifest.json').write_text(json.dumps(manifest))
        shutil.copyfile(ROOT/'examples/save.ui.json',out/'save.ui.json')
        index=json.loads((out/'scripts/index.json').read_text());index['save_ui']='save.ui.json';index['title_ui']='title.ui.json'
        shutil.copyfile(ROOT/'tests/fixtures/audio-tone.ogg',out/'tone.ogg')
        catalog=json.loads((out/'catalog.json').read_text())
        catalog['entries']['bgm/tone.ogg']={'kind':'bgm','name':'tone.ogg','tag':'bgm','path':'tone.ogg',
            'source_sha256':hashlib.sha256((out/'tone.ogg').read_bytes()).hexdigest()}
        if a.movie_return:
            if not shutil.which('ffmpeg'):
                print('SKIP: ffmpeg executable required for movie fixture');return
            subprocess.run(['ffmpeg','-v','error','-f','lavfi','-i','testsrc=size=160x120:rate=24:duration=30',
                '-f','lavfi','-i','sine=frequency=440:sample_rate=48000:duration=30','-c:v','libx264','-pix_fmt','yuv420p',
                '-c:a','aac','-shortest','-y',str(out/'fixture.mkv')],check=True)
            catalog['entries']['mov/fixture.mkv']={'kind':'video','name':'fixture.mkv','tag':'mov','path':'fixture.mkv',
                'source_sha256':hashlib.sha256((out/'fixture.mkv').read_bytes()).hexdigest()}
        (out/'catalog.json').write_text(json.dumps(catalog))
        for n,name in enumerate(entries):
            music='.playBGM tone.ogg 0 0\n' if n==0 else ''
            if a.movie_return and n==0:music+='.movie 0 fixture.mkv 800 600 f\n'
            (out/(name+'.sc')).write_text(f'.if leak != 0 bad\n.set leak = {n+1}\n{music}.stage * background.png 0 0\n.panel 1\n.message 100   章节{n}\n.message 110   下一句\n.end\n.label bad\n.message 999   状态残留\n.end\n')
            index['scripts']['scr/'+name+'.sc']={'path':name+'.sc'}
        (out/'scripts/index.json').write_text(json.dumps(index));write_fixture_scripts(out)
    saves=out/'slots';shutil.rmtree(saves,ignore_errors=True)
    x=C.CDLL('libX11.so.6');xt=C.CDLL('libXtst.so.6')
    x.XOpenDisplay.argtypes=[C.c_char_p];x.XOpenDisplay.restype=C.c_void_p;d=x.XOpenDisplay(None)
    assert d,'Run in Xvfb'
    x.XDefaultRootWindow.argtypes=[C.c_void_p];x.XDefaultRootWindow.restype=C.c_ulong
    x.XQueryTree.argtypes=[C.c_void_p,C.c_ulong,C.POINTER(C.c_ulong),C.POINTER(C.c_ulong),C.POINTER(C.POINTER(C.c_ulong)),C.POINTER(C.c_uint)]
    x.XFetchName.argtypes=[C.c_void_p,C.c_ulong,C.POINTER(C.c_char_p)]
    x.XFree.argtypes=[C.c_void_p];x.XSetInputFocus.argtypes=[C.c_void_p,C.c_ulong,C.c_int,C.c_ulong]
    x.XStringToKeysym.argtypes=[C.c_char_p];x.XStringToKeysym.restype=C.c_ulong
    x.XKeysymToKeycode.argtypes=[C.c_void_p,C.c_ulong];x.XKeysymToKeycode.restype=C.c_uint
    x.XFlush.argtypes=[C.c_void_p];x.XCloseDisplay.argtypes=[C.c_void_p]
    xt.XTestFakeKeyEvent.argtypes=[C.c_void_p,C.c_uint,C.c_int,C.c_ulong]
    xt.XTestFakeMotionEvent.argtypes=[C.c_void_p,C.c_int,C.c_int,C.c_int,C.c_ulong]
    xt.XTestFakeButtonEvent.argtypes=[C.c_void_p,C.c_uint,C.c_int,C.c_ulong]
    x.XTranslateCoordinates.argtypes=[C.c_void_p,C.c_ulong,C.c_ulong,C.c_int,C.c_int,C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_ulong)]
    active=[0]
    def focus_window(parent,depth=0):
        root,ancestor=C.c_ulong(),C.c_ulong();children=C.POINTER(C.c_ulong)();count=C.c_uint()
        if not x.XQueryTree(d,parent,C.byref(root),C.byref(ancestor),C.byref(children),C.byref(count)):return False
        ids=[children[i] for i in range(count.value)]
        if children:x.XFree(children)
        for window in ids:
            title=C.c_char_p();matched=False
            if x.XFetchName(d,window,C.byref(title)):
                value=(title.value or b'').lower()
                matched=b'ef' in value or value.endswith((b'| 4x',b'| 8x'))
                x.XFree(title)
            if matched:
                active[0]=window;x.XSetInputFocus(d,window,2,0);return True
            if depth<3 and focus_window(window,depth+1):return True
        return False
    def key(name):
        focus_window(x.XDefaultRootWindow(d))
        code=x.XKeysymToKeycode(d,x.XStringToKeysym(name.encode()))
        for down in [1,0]:xt.XTestFakeKeyEvent(d,code,down,0);x.XFlush(d);time.sleep(.04)
    def click(dx,dy):
        px,py,child=C.c_int(),C.c_int(),C.c_ulong()
        assert active[0]
        x.XTranslateCoordinates(d,active[0],x.XDefaultRootWindow(d),0,0,C.byref(px),C.byref(py),C.byref(child))
        xt.XTestFakeMotionEvent(d,-1,px.value+dx,py.value+dy,0)
        for down in [1,0]:xt.XTestFakeButtonEvent(d,1,down,0);x.XFlush(d);time.sleep(.04)
    cmd=[str(a.binary),'--content',str(a.real_content or out),'--save-dir',str(saves),'--text-speed','0',
         '--font-config',str(out/'font.json'),'--window-config',str(out/'window.json'),'--width','800','--height','600']
    if a.title_only:
        (out/'font.json').unlink(missing_ok=True)
        cmd+=['--frames','600','--screenshot',str(out/'title-frame.ppm')]
    if not a.audio:cmd.append('--silent')
    p=subprocess.Popen(cmd,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,env=dict(os.environ,XDG_CACHE_HOME=str(out/'cache'),SDL_VIDEO_DRIVER='x11',SDL_AUDIO_DRIVER='dummy',LIBGL_ALWAYS_SOFTWARE='1'))
    sel=selectors.DefaultSelector();sel.register(p.stdout,selectors.EVENT_READ);log=b'';cursor=0;latencies=[]
    def wait(token):
        nonlocal log,cursor
        began=time.monotonic();deadline=began+(60 if a.real_content else 20);token=token.encode()
        while token not in log[cursor:]:
            assert p.poll() is None,log.decode(errors='replace')
            assert time.monotonic()<deadline,log.decode(errors='replace')
            for e,_ in sel.select(.05):log+=os.read(e.fileobj.fileno(),65536)
        cursor=log.index(token,cursor)+len(token)
        latencies.append({'event':token.decode(),'seconds':round(time.monotonic()-began,4)})
    try:
        wait('[title] opened');assert not saves.exists()
        if a.title_only:
            key('Home')
            # Native rectangles use half-open bounds; x=679 must not activate New Game.
            click(679,55)
            # Press/release over different buttons must not dispatch either action.
            def move(dx,dy):
                px,py,child=C.c_int(),C.c_int(),C.c_ulong()
                x.XTranslateCoordinates(d,active[0],x.XDefaultRootWindow(d),0,0,C.byref(px),C.byref(py),C.byref(child))
                xt.XTestFakeMotionEvent(d,-1,px.value+dx,py.value+dy,0);x.XFlush(d);time.sleep(.08)
            move(740,55);xt.XTestFakeButtonEvent(d,1,1,0);x.XFlush(d);time.sleep(.08)
            move(740,85);xt.XTestFakeButtonEvent(d,1,0,0);x.XFlush(d);time.sleep(.08)
            # The System button changes real persisted font settings without starting a script.
            click(740,117);key('Home');key('Down');key('Return');wait('font applied=')
            settings=json.loads((out/'font.json').read_text());assert abs(settings['scale']-.95)<1e-6,settings
            key('Down');key('Return');wait('font applied=')
            settings=json.loads((out/'font.json').read_text());assert settings['scale']==1,settings
            key('Down');key('Return');wait('font applied=')
            key('Escape');key('F8');wait('font applied=');key('F9');wait('font applied=')
            if a.wm:
                key('F11');wait('window tier=8');key('F11');wait('window tier=4')
            key('Home')
            assert p.wait(timeout=30)==0
            log+=p.stdout.read()
            assert b'[title] chapter ' not in log and b'[save-menu] opened' not in log,log
            assert not saves.exists()
            frame=Image.open(out/'title-frame.ppm').convert('RGB');assert frame.size==(800,600)
            if not a.real_content:
                assert frame.getpixel((400,300))==(72,100,130)
                assert frame.getpixel((700,55))==(230,180,70)
                assert frame.getpixel((700,85))==(72,100,130)
                assert frame.getpixel((679,55))==(72,100,130)
            frame.save(out/'title-frame.png')
            print('Title artwork PASS: native hotzones, isolated atlas highlight, no cross-button activation, settings and fullscreen return')
            return
        if a.gallery_only:
            def open_gallery():
                key('Home')
                for _ in range(3):key('Down')
                key('Return');key('Down');key('Return');wait('[gallery] opened')
            open_gallery()
            if a.real_content:
                import re
                report=json.loads(subprocess.check_output([str(a.binary),'--content',str(a.real_content),'--audit-gallery']))
                groups=report['groups']
                wait('[gallery] thumb '+groups[11]['variants'][0]);time.sleep(.2)
                ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(out/'gallery-real-page.png')
                for page in range(1,6):
                    key('Page_Down');wait('[gallery] thumb '+groups[page*12+11]['variants'][0])
                residents=[int(n) for n in re.findall(rb'resident=(\d+)',log)]
                assert max(residents)==48 and all(n<=48 for n in residents),residents
                key('Home');key('Return');wait('[gallery] ready ');time.sleep(.2)
                ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(out/'gallery-real-view.png')
                if a.wm:
                    key('F11');wait('window tier=8');wait('[gallery] ready ')
                    key('F11');wait('window tier=4');wait('[gallery] ready ')
                    key('F10');wait('window tier=8');wait('[gallery] ready ')
                    key('F10');wait('window tier=4');wait('[gallery] ready ')
                key('equal');key('w');key('0')
                key('Right');wait('[gallery] ready ')
                key('Escape');key('End');key('Return');wait('[gallery] ready ')
            else:
                wait('[gallery] thumb bg/ev01_012a01.png')
                key('Page_Down');wait('[gallery] thumb bg/ev01_014a01.png')
                key('Home');click(110,140);wait('[gallery] ready bg/ev01_001a01.png pixels=320x240')
                time.sleep(.15)
                shot=ImageGrab.grab(xdisplay=os.environ['DISPLAY']);shot.save(out/'gallery-view.png')
                # At an 800x600 fixture window, screenshot contains the exact opaque CG color.
                assert (50,80,210) in shot.convert('RGB').getdata()
                key('Right');wait('[gallery] ready bg/ev01_001b01.png pixels=320x240')
                if a.wm:
                    key('F11');wait('window tier=8');wait('[gallery] ready bg/ev01_001b01.png pixels=640x480')
                    key('F11');wait('window tier=4');wait('[gallery] ready bg/ev01_001b01.png pixels=320x240')
                    # Maximize follows the same tier policy; a broken 8x source falls back to 4x.
                    (out/'cg-1-b01-8.webp').write_bytes(b'broken')
                    key('F10');wait('window tier=8');wait('[gallery] ready bg/ev01_001b01.png pixels=320x240')
                    key('F10');wait('window tier=4');wait('[gallery] ready bg/ev01_001b01.png pixels=320x240')
                for _ in range(8):key('Right')
                wait('[gallery] ready ')
                key('equal');key('w');key('0')
                key('Escape');key('Tab');key('Return');wait('[gallery] ready ')
            key('Escape');key('Escape');wait('[gallery] returned to title')
            assert not saves.exists(),'Gallery must not create or require save data'
            assert list((out/'cache/ef-native/gallery-v1').glob('*.png')),'No runtime thumbnail disk cache'
            # Corrupt runtime caches are regenerated; never trust a cache as game content.
            if not a.real_content:
                for cached in (out/'cache/ef-native/gallery-v1').glob('*.png'):cached.write_bytes(b'broken')
            # Closing with a decode in flight must safely join.
            open_gallery();wait('[gallery] thumb ');click(400,570);wait('[gallery] returned to title')
            (out/'gallery-timings.json').write_text(json.dumps({'waits':latencies,'process_memory':Path(f'/proc/{p.pid}/status').read_text().split('VmHWM:')[1].splitlines()[0].strip()},indent=2))
            key('Escape');key('Return');assert p.wait(timeout=10)==0
            # Capture the application's GL back buffer: Xvfb desktop grabs can omit
            # some glyphs even when the actual rendered buffer is complete.
            log+=p.stdout.read();sel.close()
            capture=out/'gallery-frame.ppm';capture.unlink(missing_ok=True)
            p=subprocess.Popen(cmd+['--frames','180','--screenshot',str(capture)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                env=dict(os.environ,XDG_CACHE_HOME=str(out/'cache'),SDL_VIDEO_DRIVER='x11',SDL_AUDIO_DRIVER='dummy',LIBGL_ALWAYS_SOFTWARE='1'))
            sel=selectors.DefaultSelector();sel.register(p.stdout,selectors.EVENT_READ);cursor=len(log)
            wait('[title] opened');open_gallery();wait('[gallery] thumb ')
            assert p.wait(timeout=15)==0
            frame=Image.open(capture).convert('RGB');assert frame.size==(800,600)
            for box in [(25,560,80,590),(295,560,365,590),(655,560,700,590)]:
                ink=sum(min(pixel)>180 for pixel in frame.crop(box).getdata())
                assert ink>30,('Missing Chinese footer glyphs in GL capture',box,ink)
            frame.save(out/'gallery-frame.png')
            print('Gallery flow PASS: empty saves, paging, variants, cache, repeated open/return; '+('HD fullscreen/maximize tiers checked' if a.wm else 'tier window checks skipped (install Openbox or pass --wm)'))
            return
        if a.real_content:
            time.sleep(.15);ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(out/'title.png')
        key('c');wait('[title] no compatible save');assert not saves.exists()
        key('Home');key('Down');key('Return');wait('[save-menu] opened title load')
        key('Tab');key('Right');key('Return');key('Escape')
        assert not saves.exists(),'Title load input created a save'
        if a.movie_return:
            for _ in range(2):
                key('Home');key('Return');wait('[movie] start mov/fixture.mkv')
                key('Escape');wait('[title] confirm return');key('Escape');wait('[title] confirmation cancelled')
                key('Escape');wait('[title] confirm return');click(300,330);wait('[title] opened; session released')
            key('Escape');key('Return');assert p.wait(timeout=10)==0
            assert b'[movie] finished' not in log,'Fixture ended before the disposal check'
            print('Title movie return PASS: live decoder worker/audio callback cancellation and repeated disposal')
            return
        # All chapters in one process: old locals must never reach the next session.
        for n,name in enumerate(entries):
            key('Home')
            for _ in range(3):key('Down')
            key('Return');key('Home');key('Return');key('Home')
            for _ in range(n):key('Down')
            key('Return');wait(f'[title] chapter {n} started');wait(f'message ready scr/{name}.sc L0=100')
            key('F2');wait('[save] saved slot q')
            catalog=json.loads((saves/'catalog.json').read_text());entry=catalog['entries']['q'+str(n+1)]
            snapshot=json.loads((saves/entry['file']).read_text())
            if a.real_content:ImageGrab.grab(xdisplay=os.environ['DISPLAY']).save(out/f'chapter-{n}.png')
            if not a.real_content:assert snapshot['vm']['locals']['leak']==n+1
            assert snapshot['vm']['globals']['EYE_SEQ_DEFAULT']==3
            assert snapshot['vm']['globals']['censored']==0
            bgm=[t['resource'] for t in snapshot['presentation']['audio']['tracks'] if t['channel']==0]
            expected=([[],['bgm/efBGM025mix.ogg'],['bgm/efBGM026mix.ogg'],[]][n] if a.real_content else ['bgm/tone.ogg'] if n==0 else [])
            assert bgm==expected,(n,bgm)
            if a.real_content and n:
                expected_image=['','bg/ev01_sky_a_sepia.png','bg/ev00_015a03.png','bg/ago_base_20001a.png'][n]
                assert expected_image in [l['asset'] for l in snapshot['presentation']['layers']]
            key('Escape');wait('[title] confirm return');key('Escape');wait('[title] confirmation cancelled')
            key('Escape');wait('[title] confirm return');click(300,330);wait('[title] opened; session released')
        # Continue chooses the latest compatible save, then returns cleanly again.
        key('c');wait('[save] loaded menu slot');wait('message ready scr/200_01.sc L0=100')
        key('Escape');wait('[title] confirm return');key('Return');wait('[title] opened; session released')
        # Populated title load menu restores q1, with no session attached beforehand.
        key('Home');key('Down');key('Return');wait('[save-menu] opened title load')
        key('Tab');key('Page_Up');key('Down');key('Down');key('Return')
        wait('[save] loaded menu slot');wait('message ready scr/000_01mugai.sc L0=100')
        key('Escape');wait('[title] confirm return');key('Return');wait('[title] opened; session released')
        # New game starts at the prologue, without a saved route or unlocked flags.
        key('Home');click(740,55);wait('[title] chapter 0 started');wait('message ready scr/000_01mugai.sc L0=100')
        key('Escape');wait('[title] confirm return');key('Return');wait('[title] opened; session released')
        key('Escape');key('Escape')  # cancel exit
        key('Escape');key('Return');assert p.wait(timeout=10)==0
        assert b'L0=110' not in log,'Menu or confirmation input advanced the story'
        print('Title flow PASS: empty saves, all chapters, isolated globals/locals, cancel/return, latest continue, new game and exit')
    finally:
        if p.poll() is None:p.kill();p.wait()
        log+=p.stdout.read();(out/'flow.log').write_bytes(log);sel.close();x.XCloseDisplay(d)


if __name__=='__main__':
    import sys
    if '--gallery-only' in sys.argv or '--title-only' in sys.argv:
        from render_smoke import window_manager
        probe=argparse.ArgumentParser(add_help=False);probe.add_argument('--out',type=Path,default=ROOT/'build/title-flow-smoke')
        probe.add_argument('--wm',default=shutil.which('openbox'))
        opts,_=probe.parse_known_args();opts.out.mkdir(parents=True,exist_ok=True)
        from contextlib import nullcontext
        with (window_manager(opts.out,opts.wm) if opts.wm else nullcontext()):main()
    else:main()
