from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/approved-redesign';O.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
 checked(op='init',settings=tmp);base=json.loads((R/'docs/test-evidence/M3/quality/portrait-highres-recipe.json').read_text());source=R/'docs/test-evidence/M3/quality/portrait-highres-source.png'
 checked(op='portrait_window',path=str(source),recipe=base);settle();checked(op='portrait_state',panel=1)
 start=time.monotonic();i=0
 while time.monotonic()-start<900:
  checked(op='pump',waitMs=100);s=checked(op='portrait_state');i+=1
  if i%10==0:(O/'native-state.json').write_text(json.dumps(s,ensure_ascii=False),encoding='utf-8')
  if (O/'stop-host').exists():break
 checked(op='portrait_state',capture=str(O/'native-final.png'));checked(op='portrait_close');checked(op='shutdown')
