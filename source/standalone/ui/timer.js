/* Shared timer editor; zero minutes means unregistered. */
(function(root){
 'use strict';
 function valid(minutes,schedule){
  if(!Array.isArray(minutes)||minutes.length!==4||minutes.some(m=>!Number.isInteger(m)||m<0||m>1440))throw Error('Timer');
  if(schedule.rules.some(r=>(r.mode&127)&&minutes.some((m,i)=>m&&(r.channel===4||r.channel===i))))throw Error('Conflict');
 }
 function mount({save,available,schedule}){
  const box=document.getElementById('timer-editor'),form=document.getElementById('timer-form'),status=document.getElementById('timer-status');let values=[0,0,0,0],loaded=false,busy=false,dirty=false;const liveNodes=[],liveTexts=[];
  function render(){box.replaceChildren();const hint=document.createElement('p');hint.className='muted';hint.textContent='Auto OFF after ON: 1 minute–24 hours. ON channels restart the full timer after reboot. A channel cannot also have a schedule.';box.append(hint);
   values.forEach((value,i)=>{const card=document.createElement('div');card.className='name-editor';const title=document.createElement('b');title.textContent='Channel '+(i+1);card.append(title);
    const conflict=schedule().rules.some(r=>(r.mode&127)&&(r.channel===4||r.channel===i));const row=document.createElement('div');row.className='row';const hours=document.createElement('input'),minutes=document.createElement('input');
    [[hours,24,Math.floor(value/60),'Hours'],[minutes,59,value%60,'Minutes']].forEach(([input,max,v,label])=>{input.type='number';input.min=0;input.max=max;input.step=1;input.value=v;input.required=true;input.disabled=busy||!loaded||conflict;input.style.width='85px';input.setAttribute('aria-label',title.textContent+' '+label);const l=document.createElement('label');l.textContent=label;l.append(input);row.append(l);});
    const update=()=>{dirty=true;values[i]=Number(hours.value)*60+Number(minutes.value);};hours.oninput=minutes.oninput=update;
    const remove=document.createElement('button');remove.type='button';remove.textContent='Delete';remove.disabled=busy||!loaded;remove.onclick=()=>{values[i]=0;dirty=true;render();};row.append(remove);card.append(row);
    const note=document.createElement('p');note.className='muted';note.textContent=conflict?'Registered in scheduler. Delete its schedules first.':value?'Registered · save to apply':'Unregistered · save zero to remove.';card.append(note);
    const live=document.createElement('div');live.className='muted';live.textContent=liveTexts[i]||'Timer 00:00:00';liveNodes[i]=live;card.append(live);box.append(card);
   });const apply=document.createElement('button');apply.type='submit';apply.className='primary';apply.textContent='Save to device';apply.disabled=busy||!loaded||!available();box.append(apply);
  }
  form.onsubmit=async e=>{e.preventDefault();if(busy||!loaded||!available())return;try{valid(values,schedule());busy=true;render();await save([...values]);dirty=false;status.textContent='Saved.';}catch(_){status.textContent='Save failed. Check 24-hour limit, schedule conflicts and connection.';}finally{busy=false;render();}};
  render();return {load(v){if(dirty||busy)return;values=[...(v||[0,0,0,0])];loaded=true;render();},show(texts){liveNodes.forEach((n,i)=>{liveTexts[i]=texts[i]||'Timer 00:00:00';n.textContent=liveTexts[i];});},refresh:render,ready(){const b=box.querySelector('button[type=submit]');if(b)b.disabled=busy||!loaded||!available();}};
 }
 root.MttlTimer={valid,mount};if(typeof module==='object'&&module.exports)module.exports=root.MttlTimer;
})(globalThis);
