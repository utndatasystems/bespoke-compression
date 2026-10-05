"""Astra and GLM paper comparison; unchanged measurements, with the full comparison archived separately."""
from pathlib import Path
from math import atan2, degrees, hypot
import csv
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.ticker import FuncFormatter,NullLocator
from matplotlib.colors import to_rgb
from matplotlib.lines import Line2D
from matplotlib.path import Path as DrawingPath
from matplotlib.transforms import Bbox
from matplotlib.font_manager import FontProperties
R=Path(__file__).resolve().parents[1]
DATA=R/'data'
PLOTS=R/'plots'
PLOTS.mkdir(exist_ok=True)
plt.rcParams.update({'font.family':'DejaVu Sans','font.size':6.5,'axes.spines.top':False,'axes.spines.right':False,'pdf.fonttype':42,'svg.fonttype':'none','svg.hashsalt':'dbtext-astra'})
# Presentation-only bulk refresh. These points retain their original measurements.
bulk=list(csv.DictReader((DATA/'bulk-points.csv').open()))
for p in bulk:
 for key in ['ratio','median','low','high']:p[key]=float(p[key])
 for key in ['median','low','high']:p[key]/=1000  # Decimal MB/s to GB/s; CSV unchanged.
 p['stage']=int(p['stage'])
def frontier(pool):
 pool=[p for p in pool if p['ratio']>1 and p['qualification']!='accepted_pinned_result_with_portability_caveat']
 return sorted([p for p in pool if not any(q['ratio']>=p['ratio'] and q['median']>=p['median'] and (q['ratio']>p['ratio'] or q['median']>p['median']) for q in pool)],key=lambda p:p['ratio'])
def baseline_color(name):
    family = name.split('-')[0].split()[0]
    return {'OnPair+':'#527D45', 'Zstd':'#8B609A', 'LZ4':'#AF5961',
            'Brotli':'#98702E', 'XZ':'#397F89', 'bzip2':'#93634C'}[family]

baseline_labels = {'OnPair+': 'OnPair+', 'Zstd-1': 'Zstandard-1',
                   'Zstd-21': 'Zstandard-21/22', 'Brotli-11': 'Brotli-11', 'bzip2-9': 'bzip2-9'}
FRONT_LINEWIDTH=1.0
COMBINED_COLOR='#243E50'
# Point colors identify the experimental setting.
policies = {'from-scratch': ('S', 'Astra: Standalone', 'D', 'white', '#0072B2', '-'),
            'tools-allowed': ('T', 'Astra: With external dependencies', 'D', 'white', '#D55E00', '-'),
            'boundary-only': ('B', 'Astra: Standalone, without reference values', 'D', 'white', '#009E73', '-'),
            'glm': ('G', 'GLM: With external dependencies', '*', 'white', '#6B5B00', '-')}


def place_labels(ax, entries, marked_points, frontiers, paper=False):
 """Choose nearby boxes clear of every frontier, point and prior label.

 Adapted from the repository's plotting/plot.py collision-aware label placement.
 Screen-space checks apply to the final figure size, including paper exports.
 """
 fig=ax.figure;fig.canvas.draw();renderer=fig.canvas.get_renderer()
 area=ax.get_window_extent();occupied=[]
 point_boxes=[Bbox.from_bounds(x-10,y-10,20,20) for x,y in
              ax.transData.transform([(p['ratio'],p['median']) for p in marked_points])]
 segments=[]
 for f in frontiers:
  xy=ax.transData.transform([(p['ratio'],p['median']) for p in f])
  segments.extend(zip(xy,xy[1:]))
 labels=[];label_keys=[]
 for p,label,col,boxed in entries:
  size=5.4 if boxed else 5.8
  w,h,_=renderer.get_text_width_height_descent(label,FontProperties(size=size),False)
  padding=12 if boxed else 5
  w+=padding;h=max(h,size*fig.dpi/72)+padding
  px,py=ax.transData.transform((p['ratio'],p['median']));choices=[]
  near=[[(0,h/2+g),(w/2+g,0),(0,-h/2-g),(-w/2-g,0),
         (w/2+g,h/2+g),(-w/2-g,h/2+g),(w/2+g,-h/2-g),(-w/2-g,-h/2-g)]
        for g in (8,16,28,44,64,88,120,150)]
  grid=[(area.x0+area.width*x/30-px,area.y0+area.height*y/30-py)
        for x in range(1,30) for y in range(1,30)]
  key=(p["arm"],p["name"] if p["arm"]=="baseline" else p["stage"])
  for candidates in near+[grid]:
   for dx,dy in candidates:
    box=Bbox.from_bounds(px+dx-w/2,py+dy-h/2,w,h)
    if not(area.x0+3<=box.x0 and box.x1<=area.x1-3 and area.y0+3<=box.y0 and box.y1<=area.y1-3):continue
    if any(box.overlaps(b.padded(3)) for b in occupied+point_boxes):continue
    if any(DrawingPath(seg).intersects_bbox(box.padded(3),filled=False) for seg in segments):continue
    choices.append((dx*dx+dy*dy,dx,dy,box))
   if choices:break
  if not choices:raise RuntimeError('No clear label position: '+label)
  _,dx,dy,box=min(choices,key=lambda c:c[0]);occupied.append(box)
  bbox=(dict(boxstyle='round,pad=.3,rounding_size=.12',
             fc=tuple(.92+.08*c for c in to_rgb(col)),ec=col,lw=.45)
        if boxed else dict(fc='white',ec='none',pad=.6))
  annotation=ax.annotate(label,(p['ratio'],p['median']),xytext=(dx*72/fig.dpi,dy*72/fig.dpi),
              textcoords='offset points',fontsize=size,color=col,ha='center',va='center',zorder=6,
              bbox=bbox,
              arrowprops=dict(arrowstyle='-',color=col,lw=.4,shrinkA=1,shrinkB=2)
              if p['arm']=='baseline' else None)
  labels.append(annotation);label_keys.append(key)
 # Preserve the paper's hand-adjusted positions when they remain clear.
 # Fresh timings can move the frontier; then use the collision-aware placement.
 automatic_positions=[a.get_position() for a in labels]
 overrides={
  ('tools-allowed',3):((8,-2.5),0,'center','center'),
  ('boundary-only',3):((0,9),0,'center','center'),
  ('boundary-only',4):((2,7.34),0,'center','center'),
  ('glm',4):((0,6.3),0,'center','center'),
  ('baseline','Zstd-21'):((4,9),30,'left','bottom'),
  ('baseline','Brotli-11'):((4,9),30,'left','bottom'),
  ('baseline','bzip2-9'):((10,7.5),30,'left','bottom'),
 }
 for label,key in zip(labels,label_keys):
  if key in overrides:
   offset,rotation,ha,va=overrides[key]
   label.set_position(offset);label.set_rotation(rotation)
   label.set_rotation_mode('anchor');label.set_ha(ha);label.set_va(va)
 fig.canvas.draw()
 boxes=[a.get_bbox_patch().get_window_extent() for a in labels]
 patches=[a.get_bbox_patch().get_path().transformed(a.get_bbox_patch().get_transform()) for a in labels]
 obstructed=any(
  any(patch.intersects_path(other,filled=True) for other in patches[:i])
  or any(DrawingPath(seg).intersects_path(patch,filled=True) for seg in segments)
  or any(patch.intersects_bbox(b,filled=True) for b in point_boxes)
  or not (area.contains(boxes[i].x0,boxes[i].y0) and area.contains(boxes[i].x1,boxes[i].y1))
  for i,patch in enumerate(patches))
 if obstructed:
  for label,offset in zip(labels,automatic_positions):
   label.set_position(offset);label.set_rotation(0)
   label.set_rotation_mode('anchor');label.set_ha('center');label.set_va('center')
 # Validate actual rendered patches too, so font or layout changes fail visibly.
 fig.canvas.draw()
 boxes=[a.get_bbox_patch().get_window_extent() for a in labels]
 patches=[a.get_bbox_patch().get_path().transformed(a.get_bbox_patch().get_transform()) for a in labels]
 for i,box in enumerate(boxes):
  patch=patches[i]
  assert not any(patch.intersects_path(other,filled=True) for other in patches[:i]),('Overlapping labels',label_keys[i],[label_keys[j] for j,other in enumerate(patches[:i]) if patch.intersects_path(other,filled=True)])
  assert not any(DrawingPath(seg).intersects_path(patch,filled=True) for seg in segments),('Label hides frontier',label_keys[i])
  assert not any(patch.intersects_bbox(b,filled=True) for b in point_boxes),('Label hides point',label_keys[i])
  assert area.contains(box.x0,box.y0) and area.contains(box.x1,box.y1),('Label outside axes',label_keys[i])
 assert all((label.arrow_patch is not None)==(key[0]=='baseline')
            for label,key in zip(labels,label_keys)),'Unexpected point-label leader'



def draw_block(arms,paper=False):
 combined=len(arms)>1
 plot_height_mm=53.46
 legend_extra_mm=2.2
 figure_height_mm=plot_height_mm+legend_extra_mm
 fig,ax=plt.subplots(figsize=(126/25.4,figure_height_mm/25.4),dpi=300)
 fig.subplots_adjust(left=.09,right=.995,
                     bottom=(.31*plot_height_mm+legend_extra_mm)/figure_height_mm,
                     top=1-.015*plot_height_mm/figure_height_mm)
 base=frontier([p for p in bulk if p['arm']=='baseline'])
 runs=[p for p in bulk if p['arm'] in arms]
 combined_front=frontier(base+runs);frontiers=[base,combined_front]
 ax.plot([p['ratio'] for p in base],[p['median'] for p in base],color='#89929A',ls=':',lw=FRONT_LINEWIDTH,zorder=1)
 ax.plot([p['ratio'] for p in combined_front],[p['median'] for p in combined_front],color=COMBINED_COLOR,ls='-',lw=FRONT_LINEWIDTH,zorder=2)
 entries=[]
 for p in base:
  col=baseline_color(p['name']);ax.plot(p['ratio'],p['median'],color=col,marker='o',ms=3.5,ls='none',zorder=3,clip_on=False)
  if p['name'] in baseline_labels:entries.append((p,baseline_labels[p['name']],col,True))
 for p in runs:
  i=p['stage'];short,_,marker,fill,col,_=policies[p['arm']]
  ax.plot(p['ratio'],p['median'],color=col,marker=marker,mfc=fill or col,mew=.8,ms=6 if marker=='*' else 4.2,ls='none',zorder=5,clip_on=False)
  entries.append((p,f'{"G" if p["arm"]=="glm" else "A"}{i}',col,False))
 ax.set_xlim(2.3,4.04);ax.set_ylim(0,7)
 ax.set_xticks([2.5,2.75,3,3.25,3.5,3.75,4]);ax.xaxis.set_major_formatter(FuncFormatter(lambda v,_:f'{v:g}×'))
 ax.set_yticks([0,2,4,6]);ax.yaxis.set_major_formatter(FuncFormatter(lambda v,_:f'{v:g}'));ax.yaxis.set_minor_locator(NullLocator())
 ax.grid(which='major',color='#E4E8EB',lw=.4);ax.set_axisbelow(True)
 ax.set_xlabel('Compression factor',labelpad=2)
 ax.set_ylabel('Decompression [GB/s]',labelpad=2)
 place_labels(ax,entries,base+runs,frontiers,paper=paper)
 # Detached direction indicator; the unequal axis dimensions need different fractions.
 ax.annotate('',xy=(.95,.92),xytext=(.825,.59),xycoords='axes fraction',
             arrowprops=dict(arrowstyle='-|>',color='#243E50',lw=1.4,mutation_scale=10),zorder=7)
 start,end=ax.transAxes.transform([(.825,.59),(.95,.92)])
 dx,dy=end-start;length=hypot(dx,dy)
 ax.annotate('Better',xy=(.8875,.755),xycoords='axes fraction',
             xytext=(-1.5*dy/length,1.5*dx/length),textcoords='offset points',
             fontsize=8,color='#243E50',rotation=degrees(atan2(dy,dx)),
             rotation_mode='anchor',ha='center',va='bottom',zorder=7)
 legend_arms=[a for a in ('boundary-only','from-scratch','tools-allowed','glm') if a in arms]
 handles=[Line2D([],[],marker=policies[a][2],color=policies[a][4],mfc='white',mew=.8,ms=6 if policies[a][2]=='*' else 4.2,ls='none',label=policies[a][1]) for a in legend_arms]
 handles.append(Line2D([],[],color='#89929A',ls=':',lw=FRONT_LINEWIDTH,label='Traditional front'))
 handles.append(Line2D([],[],color=COMBINED_COLOR,ls='-',lw=FRONT_LINEWIDTH,label='Combined front'))
 fig.legend(handles=handles,loc='lower center',bbox_to_anchor=(.54,.002),ncol=2,frameon=False,fontsize=5.6,labelspacing=.45,columnspacing=1.25,handlelength=1.6,handletextpad=.5)
 if not paper:
  fig.text(.075,.96,'DBText · Astra, Luna and GLM · Block-based compression',fontsize=21,weight='bold',color='#152E3D')
 name='dbtext-astra-glm'
 if paper:fig.savefig((R if combined else PLOTS)/(name+'-paper-ready.pdf'),facecolor='white')
 else:
  for ext in ['png','pdf','svg']:fig.savefig(PLOTS/(name+'.'+ext),dpi=200,facecolor='white')
 fig.savefig(R/'astra-glm-layout-preview.png',dpi=240)
 plt.close(fig)


for paper in [True]:
 draw_block(list(policies),paper)
report={a:[{'name':p['name'],'arm':p['arm'],'ratio':p['ratio'],'decode_GB_s':p['median']} for p in frontier([p for p in bulk if p['arm'] in ('baseline',a)])] for a in policies}
report['combined']=[{'name':p['name'],'arm':p['arm'],'ratio':p['ratio'],'decode_GB_s':p['median']} for p in frontier(bulk)]
import json
(R/'frontiers-astra-glm.json').write_text(json.dumps(report,indent=2)+'\n')
assert all(p['stage']!=5 for p in bulk)
assert all(sum(p['arm']==a for p in bulk)==4 for a in policies)
print(json.dumps({a:[p['name'] for p in f if a=='combined' or p['arm']==a] for a,f in report.items()}))
