from pathlib import Path
exec(Path('tools/m3_geometry_validate.py').read_text(encoding='utf-8').split('with tempfile.TemporaryDirectory() as tmp:')[0])
O=R/'docs/test-evidence/M3/approved-redesign';O.mkdir(parents=True,exist_ok=True)
with tempfile.TemporaryDirectory(dir=R/"build",ignore_cleanup_errors=True) as tmp:
 checked(op='init',settings=tmp)
 base=json.loads((R/'docs/test-evidence/M3/quality/portrait-highres-recipe.json').read_text())
 source=R/'docs/test-evidence/M3/quality/portrait-highres-source.png'
 checked(op='portrait_window',path=str(source),recipe=base)
 settle();checked(op='portrait_state',panel=1);checked(op='pump',waitMs=1000)
 checked(op='portrait_state',capture=str(O/('navigation-icons-'+os.environ.get('QT_SCALE_FACTOR','native')+'.png')))
 print('ready',flush=True)
 checked(op='portrait_close');checked(op='shutdown')
