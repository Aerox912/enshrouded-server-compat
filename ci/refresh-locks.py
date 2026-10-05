"""Update only approved maintained dependencies after a published component release."""
import argparse,hashlib,io,json,os,subprocess,urllib.request,zipfile
from pathlib import Path

REPOS={'minimap':'Aerox912/Enshrouded-minimap','first-person':'Aerox912/shroudtopia','patches':'Aerox912/enshrouded-mod-patches'}
def main():
 p=argparse.ArgumentParser();p.add_argument('--component',choices=REPOS,required=True);p.add_argument('--tag',required=True);args=p.parse_args()
 if not args.tag or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._' for c in args.tag):raise ValueError('Invalid release tag')
 release=json.loads(subprocess.check_output(['gh','api',f'repos/{REPOS[args.component]}/releases/tags/{args.tag}']))
 if release['draft'] or release['prerelease']:raise ValueError('Dependency is not a stable published release')
 assets=[a for a in release['assets'] if a['name'].endswith('.zip')]
 if len(assets)!=1:raise ValueError('Expected one unambiguous component ZIP')
 a=assets[0]
 if a['size']>50000000:raise ValueError('Unexpected component size')
 with urllib.request.urlopen(a['browser_download_url'],timeout=90) as r:data=r.read(a['size']+1)
 if len(data)!=a['size']:raise ValueError('Incomplete release download')
 digest=hashlib.sha256(data).hexdigest()
 if a.get('digest') and a['digest']!='sha256:'+digest:raise ValueError('GitHub asset digest mismatch')
 with zipfile.ZipFile(io.BytesIO(data)) as z:info=json.loads(z.read('build-info.json').decode('utf-8-sig'))
 commit=info['commit']
 if len(commit)!=40 or any(c not in '0123456789abcdef' for c in commit):raise ValueError('Invalid build provenance')
 lockfile=Path('dependencies.lock.json');lock=json.loads(lockfile.read_text(encoding='utf-8-sig'))
 item={'tag':args.tag,'url':a['browser_download_url'],'sha256':digest,'size':a['size'],'commit':commit}
 if 'releases' in lock:
  if lock['releases'][args.component]['tag']==args.tag:return
  lock['releases'][args.component]=item
  if args.component=='patches':
   tool=next(a for a in release['assets'] if a['name']=='PatchTool.exe')
   with urllib.request.urlopen(tool['browser_download_url'],timeout=90) as r:binary=r.read(tool['size']+1)
   if len(binary)!=tool['size']:raise ValueError('Incomplete patch tool')
   lock['patchTool']={'url':tool['browser_download_url'],'sha256':hashlib.sha256(binary).hexdigest(),'size':tool['size']}
  major,minor,patch=map(int,lock['version'].split('.'));lock['version']=f'{major}.{minor}.{patch+1}'
 else:
  if args.component!='patches':raise ValueError('Server packages only consume the patch catalog')
  if lock['patches'].get('tag')==args.tag or lock['patches']['sha256']==digest:return
  lock['patches']=item
  versionfile=Path('version.txt');major,minor,patch=map(int,versionfile.read_text().strip().split('.'));versionfile.write_text(f'{major}.{minor}.{patch+1}\n')
 lockfile.write_text(json.dumps(lock,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
