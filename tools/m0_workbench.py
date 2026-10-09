"""Python/Tk M0 session and single-person targeting workbench, not product UI."""
import argparse
import copy
import json
import os
from pathlib import Path
import tempfile
import tkinter as tk
from tkinter import filedialog, messagebox

import numpy as np
from PIL import Image, ImageDraw, ImageGrab, ImageTk

ROOT = Path(__file__).resolve().parents[1]


def detect_people(image):
    """Full image plus overlapping crops; boxes always returned in source coordinates."""
    import mediapipe as mp
    options = mp.tasks.vision.FaceLandmarkerOptions(
        base_options=mp.tasks.BaseOptions(model_asset_path=str(ROOT/'research/m0/face_landmarker.task')),
        num_faces=8)
    width, height = image.size
    windows = [(0, 0, width, height)]
    # Smaller faces need a second detection pass before the user can select them.
    for fy in (0., .4):
        for fx in (0., .3, .6):
            windows.append((int(width*fx), int(height*fy),
                            min(width, int(width*(fx+.4))), min(height, int(height*(fy+.6)))))
    boxes = []
    with mp.tasks.vision.FaceLandmarker.create_from_options(options) as detector:
        for x0, y0, x1, y1 in windows:
            crop = image.crop((x0, y0, x1, y1))
            result = detector.detect(mp.Image(image_format=mp.ImageFormat.SRGB,
                                               data=np.asarray(crop).copy()))
            for face in result.face_landmarks:
                xs = [p.x*(x1-x0)+x0 for p in face]
                ys = [p.y*(y1-y0)+y0 for p in face]
                box = (max(0, int(min(xs))), max(0, int(min(ys))),
                       min(width, int(max(xs))+1), min(height, int(max(ys))+1))
                if box[2] > box[0] and box[3] > box[1]:
                    boxes.append(box)
    kept = []
    for box in sorted(boxes, key=lambda b: -(b[2]-b[0])*(b[3]-b[1])):
        def duplicate(other):
            intersection = max(0,min(box[2],other[2])-max(box[0],other[0])) * max(0,min(box[3],other[3])-max(box[1],other[1]))
            smaller = min((box[2]-box[0])*(box[3]-box[1]), (other[2]-other[0])*(other[3]-other[1]))
            return intersection / smaller > .55
        if not any(duplicate(other) for other in kept):
            kept.append(box)
    return sorted(kept, key=lambda b: (b[0], b[1]))


class Document:
    def __init__(self):
        self.state = {'raw': 0, 'people': {}}
        self.revision = 0
        self.history = []

    def commit(self, base, draft):
        if base != self.revision:
            return False
        if draft != self.state:
            self.history.append(copy.deepcopy(self.state))
            self.state = copy.deepcopy(draft)
            self.revision += 1
        return True

    def undo(self):
        if self.history:
            self.state = self.history.pop()
            self.revision += 1


def save_state(path, state, fail=False):
    path = Path(path)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', encoding='utf-8', dir=path.parent,
                                         prefix='.huelith-m0-', delete=False) as stream:
            temporary = Path(stream.name)
            json.dump({'schema': 1, 'state': state}, stream, ensure_ascii=False)
            stream.flush()
            os.fsync(stream.fileno())
        if fail:
            raise OSError('M0 injected failure before replace')
        os.replace(temporary, path)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


class Session:
    def __init__(self, owner, portrait):
        self.owner, self.portrait = owner, portrait
        self.document = owner.document
        self.base = owner.document.revision
        self.draft = copy.deepcopy(owner.document.state)
        self.selected = None
        self.window = tk.Toplevel(owner.root)
        self.window.title('Huelith · 人像范围验证' if portrait else 'Huelith · RAW 会话验证')
        self.window.configure(bg='#24292e')
        self.window.geometry('1080x720')
        self.window.transient(owner.root)
        self.window.grab_set()
        self.window.protocol('WM_DELETE_WINDOW', self.cancel)
        self.window.bind('<Escape>', lambda _: self.cancel())
        self.photos = []
        top = tk.Frame(self.window, bg='#24292e'); top.pack(fill='x')
        tk.Label(top, text='一次只编辑一个人；切换保留各人的会话草稿。确定一次提交，取消全部丢弃。',
                 bg='#24292e', fg='white').pack(side='left', padx=12, pady=10)
        body = tk.Frame(self.window, bg='#24292e'); body.pack(fill='both', expand=True)
        self.list = tk.Frame(body, bg='#24292e'); self.list.pack(side='left', fill='y')
        self.preview = tk.Label(body, bg='#191d21'); self.preview.pack(side='left', fill='both', expand=True)
        settings = tk.Frame(body, bg='#24292e'); settings.pack(side='right', fill='y', padx=12)
        self.caption = tk.Label(settings, text='请先选择一张人脸缩略图', bg='#24292e', fg='white')
        self.caption.pack(pady=12)
        tk.Label(settings, text='范围覆盖强度（验证参数）\n不改变原图，不是美颜效果',
                 bg='#24292e', fg='white').pack()
        self.value = tk.IntVar(value=0)
        self.scale = tk.Scale(settings, from_=0, to=100, orient='horizontal', variable=self.value,
                              command=self.change, state='disabled', bg='#24292e', fg='white',
                              highlightthickness=0)
        self.scale.pack()
        self.status = tk.Label(settings, text='', wraplength=190, bg='#24292e', fg='#ffbb77')
        self.status.pack(pady=10)
        tk.Button(settings, text='确定', command=self.accept).pack(fill='x', pady=5)
        tk.Button(settings, text='取消', command=self.cancel).pack(fill='x')
        if portrait:
            for identifier, box in owner.people.items():
                thumbnail = owner.image.crop(box); thumbnail.thumbnail((88,88))
                photo = ImageTk.PhotoImage(thumbnail); self.photos.append(photo)
                tk.Button(self.list, image=photo, text=identifier, compound='top',
                          command=lambda key=identifier: self.select(key)).pack(padx=8,pady=5)
            if not owner.people:
                self.status.config(text='未检测到人脸；不会自动把参数应用到整图。')
        else:
            self.selected = 'raw'; self.scale.config(state='normal')
            self.caption.config(text='RAW 草稿参数（不执行显影）')
            self.value.set(self.draft['raw'])
        self.render()

    def select(self, identifier):
        if identifier not in self.owner.people:
            raise ValueError('unknown person')
        self.selected = identifier
        self.caption.config(text=f'当前人物：{identifier}')
        self.scale.config(state='normal')
        self.value.set(self.draft['people'].get(identifier, 0))
        self.render()

    def change(self, value):
        if self.selected is None:
            return
        if self.portrait:
            self.draft['people'][self.selected] = int(float(value))
        else:
            self.draft['raw'] = int(float(value))
        self.render()

    def render(self):
        picture = self.owner.image.convert('RGBA')
        overlay = Image.new('RGBA', picture.size)
        painter = ImageDraw.Draw(overlay)
        if self.portrait:
            for identifier, box in self.owner.people.items():
                strength = self.draft['people'].get(identifier, 0)
                if strength:
                    painter.ellipse(box, fill=(255,65,65,round(strength*1.5)))
                painter.rectangle(box, outline='#4cb8ff' if identifier == self.selected else '#999999', width=3)
                painter.text((box[0],max(0,box[1]-15)), identifier, fill='white')
        picture = Image.alpha_composite(picture, overlay); picture.thumbnail((660,550))
        self.preview_photo = ImageTk.PhotoImage(picture)
        self.preview.config(image=self.preview_photo)

    def accept(self):
        if self.owner.document is not self.document or not self.document.commit(self.base, self.draft):
            self.status.config(text='主文档已变化，旧会话未提交。请取消后重开。')
            return False
        self.cancel(); self.owner.refresh(); return True

    def cancel(self):
        self.window.grab_release(); self.window.destroy()


class Workbench:
    def __init__(self, root):
        self.root, self.document = root, Document()
        with Image.open(ROOT/'research/m0/portrait-astronaut.png') as source:
            original = np.asarray(source.convert('RGB'))
        self.image = Image.fromarray(np.concatenate([original, original[:,::-1]], axis=1))
        self.people = {}
        root.title('Huelith · M0 Python 验证工作台')
        root.geometry('900x540'); root.configure(bg='#24292e')
        tk.Label(root, text='M0 Python 验证工作台', font=('Microsoft YaHei UI',20),
                 fg='white', bg='#24292e').pack(pady=20)
        tk.Label(root, text='仅验证会话与单人目标隔离；正式产品仍为 C++。\n默认样片：NASA 公有领域照片的双人拼接。',
                 fg='white', bg='#24292e').pack()
        self.status = tk.Label(root, fg='white', bg='#24292e', wraplength=820); self.status.pack(pady=20)
        for label, command in [('打开图片并检测人物',self.open_image),('检测默认双人样片',self.detect),
                               ('打开人像验证会话',lambda: Session(self,True)),
                               ('打开 RAW 验证会话',lambda: Session(self,False)),
                               ('撤销一次提交',self.undo),('保存原型状态',self.save)]:
            tk.Button(root,text=label,command=command,width=32).pack(pady=3)
        self.refresh()

    def detect(self):
        self.status.config(text='正在检测全图与分区……'); self.root.update_idletasks()
        try:
            boxes = detect_people(self.image)
            self.people = {f'P{i+1}':box for i,box in enumerate(boxes)}
            # New detection establishes a new target set, invalidating prior drafts.
            self.document.state['people'] = {}; self.document.history.clear(); self.document.revision += 1
            self.refresh()
        except Exception as error:
            messagebox.showerror('检测失败',str(error),parent=self.root)

    def open_image(self):
        path = filedialog.askopenfilename(parent=self.root,filetypes=[('图像','*.jpg *.jpeg *.png')])
        if path:
            try:
                from PIL import ImageOps
                with Image.open(path) as source:
                    self.image = ImageOps.exif_transpose(source).convert('RGB')
                self.people = {}; self.document = Document(); self.detect()
            except Exception as error:
                messagebox.showerror('打开失败',str(error),parent=self.root)

    def refresh(self):
        self.status.config(text=f'检测到 {len(self.people)} 人；修订 {self.document.revision}\n{self.document.state}')

    def undo(self):
        self.document.undo(); self.refresh()

    def save(self):
        path = filedialog.asksaveasfilename(parent=self.root,defaultextension='.json')
        if path:
            try:
                save_state(path,self.document.state)
            except OSError as error:
                messagebox.showerror('保存失败',str(error),parent=self.root)


def self_test(app, output):
    output.mkdir(parents=True,exist_ok=True)
    boxes = detect_people(app.image)
    assert len(boxes) == 2, f'expected two default faces, got {len(boxes)}'
    app.people = {f'P{i+1}':box for i,box in enumerate(boxes)}
    initial = copy.deepcopy(app.document.state)
    session = Session(app,True)
    assert session.selected is None
    session.change(99); assert session.draft == initial
    session.select('P1'); session.change(30)
    session.select('P2'); assert session.value.get() == 0
    session.change(70); session.select('P1'); assert session.value.get() == 30
    assert app.document.state == initial
    app.root.update()
    box = (session.window.winfo_rootx(),session.window.winfo_rooty(),
           session.window.winfo_rootx()+session.window.winfo_width(),
           session.window.winfo_rooty()+session.window.winfo_height())
    screenshot_error = None
    try:
        ImageGrab.grab(bbox=box,all_screens=True).save(output/'python-person-session.png')
    except OSError as error:
        screenshot_error = str(error)
    session.cancel(); assert app.document.state == initial
    session = Session(app,True); session.select('P2'); session.change(65)
    assert session.accept(); assert app.document.state['people'] == {'P2':65}
    app.undo(); assert app.document.state == initial
    session = Session(app,False); session.change(15)
    app.document.revision += 1
    assert not session.accept(); assert app.document.state == initial; session.cancel()
    session = Session(app,True); session.select('P1'); session.change(80)
    replacement = Document(); replacement.revision = session.base
    app.document = replacement
    assert not session.accept(); assert app.document.state == initial; session.cancel()
    with tempfile.TemporaryDirectory(prefix='huelith-m0-') as directory:
        path = Path(directory)/'中文状态.json'; save_state(path,initial)
        before = path.read_bytes()
        try:
            save_state(path,{'raw':99},fail=True)
        except OSError:
            pass
        assert path.read_bytes() == before
        assert json.loads(before)['state'] == initial
    report = dict(passed=True,detected_people=app.people, screenshot_error=screenshot_error,
        checks=['no selection is inert','P1/P2 independent drafts','switch restores parameters',
                'cancel preserves main','commit changes selected person only','one undo restores',
                'stale RAW session rejected','different document rejected at same revision',
                'Chinese path save roundtrip','failed replacement preserves old'],
        limitations=['Tk UI is validation-only, not final Qt design.',
                     'Face ellipse is targeting visualization, not skin segmentation.',
                     'Two duplicated faces are not a broad detection quality benchmark.',
                     'Python behavior does not replace C++ implementation tests.'])
    (output/'python-workbench.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps(report),flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--self-test',type=Path)
    args = parser.parse_args()
    root = tk.Tk(); app = Workbench(root)
    if args.self_test:
        try:
            self_test(app,args.self_test)
        finally:
            root.destroy()
    else:
        root.mainloop()
