"""Synthetic F2/F3, failed-load isolation, NVL pixels and command-line restore in Xvfb."""
import argparse
import ctypes as C
import json
import os
from pathlib import Path
import selectors
import shutil
import subprocess
import time
from PIL import Image
from message_ui_smoke import ROOT, prepare_fixture
from script_json import write_fixture_scripts


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=ROOT/'build/dev/ef_native')
    parser.add_argument('--out', type=Path, default=ROOT/'build/save-smoke')
    args = parser.parse_args()
    out = args.out
    prepare_fixture(out)
    # Save menu uses synthetic images at the observed native design dimensions.
    manifest=json.loads((out/'manifest.json').read_text())
    for name,size in [('saveloadBase',(800,600)),('saveloadBase0',(800,600)),('saveloadSelect',(344,98)),('notsaved',(100,75))]:
        image=Image.new('RGBA',size,(20,35,70,255) if 'Base' in name else (100,80,100,180))
        filename=name+'.png';image.save(out/filename)
        manifest['images']['sys/'+filename]={'design_size':list(size),'variants':{'1':{'path':filename,'pixels':list(size)}}}
    (out/'manifest.json').write_text(json.dumps(manifest))
    shutil.copyfile(ROOT/'examples/save.ui.json',out/'save.ui.json')
    source = '.set route = 0\n.stage * background.png 0 0\n.panel 2\n.message 10   第一段\n.message 10   字面\\\\w与第二段\n.select 左:left 右:right\n.label left\n.set route = 10\n.message 20   左线\n.end\n.label right\n.set route = 20\n.message 30   右线\n.end\n'
    (out/'save.sc').write_text(source)
    index = json.loads((out/'scripts/index.json').read_text())
    index['save_ui']='save.ui.json'
    index['scripts']['scr/save.sc'] = {'path': 'save.sc'}
    (out/'scripts/index.json').write_text(json.dumps(index))
    write_fixture_scripts(out)
    slot, original = out/'quick.json', out/'original.json'
    slot.unlink(missing_ok=True)
    env = dict(os.environ, SDL_VIDEO_DRIVER='x11', LIBGL_ALWAYS_SOFTWARE='1')
    x, xt = C.CDLL('libX11.so.6'), C.CDLL('libXtst.so.6')
    x.XOpenDisplay.argtypes = [C.c_char_p]; x.XOpenDisplay.restype = C.c_void_p
    display = x.XOpenDisplay(None)
    if not display:
        raise RuntimeError('Run this test in Xvfb')
    x.XDefaultRootWindow.argtypes = [C.c_void_p]; x.XDefaultRootWindow.restype = C.c_ulong
    x.XQueryTree.argtypes = [C.c_void_p,C.c_ulong,C.POINTER(C.c_ulong),C.POINTER(C.c_ulong),C.POINTER(C.POINTER(C.c_ulong)),C.POINTER(C.c_uint)]
    x.XFetchName.argtypes = [C.c_void_p,C.c_ulong,C.POINTER(C.c_char_p)]
    x.XSetInputFocus.argtypes = [C.c_void_p,C.c_ulong,C.c_int,C.c_ulong]
    x.XFree.argtypes = [C.c_void_p]
    x.XFlush.argtypes = [C.c_void_p]
    x.XStringToKeysym.argtypes = [C.c_char_p]; x.XStringToKeysym.restype = C.c_ulong
    x.XKeysymToKeycode.argtypes = [C.c_void_p,C.c_ulong]; x.XKeysymToKeycode.restype = C.c_uint
    x.XCloseDisplay.argtypes = [C.c_void_p]
    xt.XTestFakeKeyEvent.argtypes = [C.c_void_p,C.c_uint,C.c_int,C.c_ulong]

    xt.XTestFakeMotionEvent.argtypes = [C.c_void_p,C.c_int,C.c_int,C.c_int,C.c_ulong]
    xt.XTestFakeButtonEvent.argtypes = [C.c_void_p,C.c_uint,C.c_int,C.c_ulong]
    x.XTranslateCoordinates.argtypes = [C.c_void_p,C.c_ulong,C.c_ulong,C.c_int,C.c_int,C.POINTER(C.c_int),C.POINTER(C.c_int),C.POINTER(C.c_ulong)]
    active_window, current_size = [0], [1000,750]

    def click(dx,dy):
        px,py,child=C.c_int(),C.c_int(),C.c_ulong()
        assert active_window[0]
        x.XTranslateCoordinates(display,active_window[0],x.XDefaultRootWindow(display),0,0,C.byref(px),C.byref(py),C.byref(child))
        w,h=current_size;scale=min(w/800,h/600)
        xt.XTestFakeMotionEvent(display,-1,px.value+round((w-800*scale)/2+dx*scale),py.value+round((h-600*scale)/2+dy*scale),0)
        xt.XTestFakeButtonEvent(display,1,1,0);x.XFlush(display);time.sleep(.04)
        xt.XTestFakeButtonEvent(display,1,0,0);x.XFlush(display);time.sleep(.04)

    def key(name):
        root, parent = C.c_ulong(), C.c_ulong()
        children, count = C.POINTER(C.c_ulong)(), C.c_uint()
        x.XQueryTree(display,x.XDefaultRootWindow(display),C.byref(root),C.byref(parent),C.byref(children),C.byref(count))
        try:
            for i in range(count.value):
                title = C.c_char_p()
                if x.XFetchName(display,children[i],C.byref(title)):
                    try:
                        if title.value and b'ef' in title.value.lower():
                            active_window[0]=children[i]
                            x.XSetInputFocus(display,children[i],2,0)
                    finally:
                        x.XFree(title)
        finally:
            if children:
                x.XFree(children)
        code = x.XKeysymToKeycode(display,x.XStringToKeysym(name.encode()))
        xt.XTestFakeKeyEvent(display,code,1,0); x.XFlush(display); time.sleep(.03)
        xt.XTestFakeKeyEvent(display,code,0,0); x.XFlush(display); time.sleep(.03)

    def run(name, callback, frames=100, screenshot=None, managed=False, width=1000):
        current_size[:]=[width,750]
        cmd = [str(args.binary),'--content',str(out),'--script','scr/save.sc','--silent','--text-speed','0',
               '--frames',str(frames),'--window-config',str(out/'window.json'),'--width',str(width),'--height','750']
        cmd += ['--save-dir',str(out/'managed')] if managed else ['--save-file',str(slot)]
        if screenshot:
            cmd += ['--screenshot',str(screenshot)]
        p = subprocess.Popen(cmd,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
        output, step, cursor = b'', 0, 0
        selector = selectors.DefaultSelector(); selector.register(p.stdout,selectors.EVENT_READ)
        deadline = time.monotonic()+25
        try:
            while p.poll() is None:
                if time.monotonic()>deadline:
                    raise RuntimeError('Save UI test timed out')
                for event, _ in selector.select(.05):
                    output += os.read(event.fileobj.fileno(),65536)
                next_step = callback(step,output[cursor:])
                if next_step != step:
                    step, cursor = next_step, len(output)
            output += p.stdout.read()
            (out/(name+'.log')).write_bytes(output)
            if p.returncode:
                raise AssertionError(output.decode(errors='replace'))
            return step
        finally:
            selector.close()
            if p.poll() is None:
                p.kill(); p.wait()

    def ready(data, line, id=10):
        token = f'message scr/save.sc L0={id} source_line={line}'.encode()
        at = data.find(token)
        return at>=0 and data.find(f'message ready scr/save.sc L0={id}'.encode(),at)>=0

    def capture(step, data):
        if step==0 and ready(data,4): key('space'); return 1
        if step==1 and ready(data,5): key('F2'); return 2
        if step==2 and b'[save] saved' in data:
            snapshot = json.loads(slot.read_text())
            assert snapshot['vm']['anchor']['source_line']==5
            assert snapshot['message_ui']['prefix']=='第一段\n'
            assert snapshot['message_ui']['current_text']=='字面\\w与第二段'
            shutil.copyfile(slot,original)
            return 3
        return step

    def restore(step, data):
        if step==0 and ready(data,4): key('F3'); return 1
        if step==1 and b'[save] loaded' in data:
            return 2 if not ready(data,5) else corrupt()
        if step==2 and ready(data,5): return corrupt()
        if step==3 and b'[save] Unsupported save format/version' in data:
            broken = json.loads(original.read_text())
            broken['presentation']['layers'][0]['asset'] = 'sys/msgPanel.png'
            (out/'sys-msgPanel.png').rename(out/'unavailable.png')
            slot.write_text(json.dumps(broken)); key('F3'); return 8
        if step==8 and b'[save] No usable image variant for sys/msgPanel.png' in data:
            (out/'unavailable.png').rename(out/'sys-msgPanel.png')
            key('space'); return 4
        if step==4 and b'choices count=2' in data: key('1'); return 5
        if step==5 and ready(data,9,20): key('F2'); return 6
        if step==6 and b'[save] saved' in data:
            snapshot = json.loads(slot.read_text()); assert snapshot['vm']['locals']['route']==10
            assert snapshot['vm']['message']['id']==20
            key('Escape'); return 7
        return step

    def corrupt():
        data = json.loads(slot.read_text()); data['version']=999; slot.write_text(json.dumps(data))
        key('F3'); return 3

    try:
        baseline = out/'before.ppm'
        assert run('capture',capture,screenshot=baseline)==3
        assert run('restore',restore,frames=300)==7
        restored = out/'restored.ppm'
        result = subprocess.run([str(args.binary),'--content',str(out),'--load-save',str(original),'--silent','--frames','3',
                                 '--window-config',str(out/'window.json'),'--width','1000','--height','750','--screenshot',str(restored)],
                                env=env,capture_output=True,timeout=20)
        (out/'cli-load.log').write_bytes(result.stdout+result.stderr)
        assert result.returncode==0, result.stderr.decode(errors='replace')
        with Image.open(baseline) as a, Image.open(restored) as b:
            # Speakerless NVL prefix + beginning of literal \\w; excludes animated pause marker and save notice.
            region=(150,0,375,110)
            assert a.convert('RGB').crop(region).tobytes()==b.convert('RGB').crop(region).tobytes(), 'Restored NVL page pixels differ'
        bad = json.loads(original.read_text()); bad['vm']['program'] = ''
        (out/'empty-program.json').write_text(json.dumps(bad))
        invalid = subprocess.run([str(args.binary),'--content',str(out),'--load-save',str(out/'empty-program.json'),'--trace-messages','1'],
                                 env=env,capture_output=True,timeout=10)
        assert invalid.returncode==1, invalid.stderr.decode(errors='replace')
        managed_root=out/'managed'
        shutil.rmtree(managed_root,ignore_errors=True)
        def quick_ring(step,data):
            if step==0 and ready(data,4): key('F2');return 1
            if 1<=step<=10 and f'[save] saved slot q{(step-1)%9+1}'.encode() in data:
                if step<10: key('F2');return step+1
                key('F3');return 11
            if step==11 and b'[save] loaded latest quick slot' in data: key('Escape');return 12
            return step
        assert run('managed-quick',quick_ring,frames=300,managed=True)==12
        catalog=json.loads((managed_root/'catalog.json').read_text())
        assert catalog['sequence']==10 and catalog['latest_quick']==1 and len(catalog['entries'])==9
        assert len(list((managed_root/'objects').glob('slot-*.json')))==9
        def cli(*flags,ok=True):
            result=subprocess.run([str(args.binary),'--content',str(out),*map(str,flags)],env=env,capture_output=True,timeout=15)
            assert (result.returncode==0)==ok, (flags,result.stdout,result.stderr)
            return result.stdout
        summary=json.loads(cli('--save-dir',managed_root,'--list-saves'))
        assert len(summary['slots'])==109 and sum(r['status']=='compatible' for r in summary['slots'])==9
        cli('--save-dir',managed_root,'--load-slot','latest','--trace-messages','1')
        assert json.loads((managed_root/'catalog.json').read_text())==catalog, 'Reading changed quick history'
        cli('--load-save',original,'--save-dir',managed_root,'--save-slot','100','--trace-messages','1')
        cli('--save-dir',managed_root,'--load-slot','100','--trace-messages','1')
        assert json.loads((managed_root/'catalog.json').read_text())['sequence']==11
        for flags in [('--save-slot','101'),('--load-slot','q10'),('--save-file',slot,'--save-dir',managed_root),('--list-saves','--script','scr/save.sc')]:
            cli(*flags,ok=False)
        absent=out/'absent-slots';shutil.rmtree(absent,ignore_errors=True)
        assert json.loads(cli('--save-dir',absent,'--list-saves'))['sequence']==0 and not absent.exists()
        # Modal save/load menu: selecting then confirming cannot leak Enter into the VM.
        def menu_save(step,data):
            if step==0 and ready(data,4): key('F4');return 1
            if step==1 and b'[save-menu] opened save' in data:
                key('End');key('Right');key('Return');key('Escape');key('Return');key('Return');return 2
            if step==2 and b'[save] saved menu slot 91' in data:
                key('F12');return 3
            if step==3 and b'[save-menu] opened load' in data:
                key('End');key('Right');key('Return');return 4
            if step==4 and b'[save] loaded menu slot' in data:
                key('F4');return 5
            if step==5 and b'[save-menu] opened save' in data:
                key('Right');key('Return');key('Escape');key('Escape');return 6
            return step
        assert run('modal-menu',menu_save,frames=400,managed=True)==6
        catalog=json.loads((managed_root/'catalog.json').read_text())
        assert catalog['sequence']==12, 'Cancelled menu/confirmation wrote to disk'
        data=json.loads((managed_root/catalog['entries']['91']['file']).read_text())
        assert data['vm']['anchor']['source_line']==4, 'Menu input advanced the story'
        thumb=data['thumbnail'];assert thumb['width']==100 and thumb['height']==75 and len(thumb['pixels'])==45000
        pixels=bytes.fromhex(thumb['pixels'])
        assert tuple(pixels[3*(20*100+50):3*(20*100+50)+3])==(32,56,112), 'Thumbnail lost NVL composition or captured menu'
        # Observe a populated menu, including the exact saved thumbnail pixels.
        def show_menu(step,data):
            if step==0 and ready(data,4):key('F12');return 1
            if step==1 and b'[save-menu] opened load' in data:key('End');return 2
            return step
        menu_capture=out/'menu.ppm'
        assert run('menu-pixels',show_menu,frames=100,screenshot=menu_capture,managed=True)==2
        with Image.open(menu_capture) as image:
            # 1000x750 window: thumb design (43,39), top-down source pixel (50,20).
            actual=image.convert('RGB').getpixel((116,74))
            assert all(abs(a-b)<=2 for a,b in zip(actual,(32,56,112))), actual
        def mouse_menu(step,data):
            if step==0 and ready(data,4):key('F12');return 1
            if step==1 and b'[save-menu] opened load' in data:
                click(70,575)  # switch to saving with the mouse
                click(-30,50)  # left letterbox: must not select the first slot
                click(200,70);click(200,70)  # select then activate slot 1
                click(480,330)  # cancel confirmation
                click(200,70);click(300,330)  # activate and confirm
                return 2
            if step==2 and b'[save] saved menu slot 1' in data:key('Escape');return 3
            return step
        assert run('mouse-letterbox',mouse_menu,frames=180,managed=True,width=1200)==3
        updated=json.loads((managed_root/'catalog.json').read_text())
        assert updated['sequence']==13 and '1' in updated['entries']
        print('Save UI PASS: modal paging/save/load, mouse mode switch/letterbox/confirmation, atomic thumbnail pixels, audio pause unit coverage; F2/F3, repeated-L0 NVL/literal text pixels, corrupt/missing-image load isolation, choice state, 9-slot quick rotation, manual slot 100, read-only listing and CLI restart')
    finally:
        x.XCloseDisplay(display)


if __name__=='__main__':
    main()
