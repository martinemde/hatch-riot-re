// Run with node flasher/test_dashboard.cjs. No browser, server, or device access.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const elements = new Map();
function element(id) {
  if (!elements.has(id)) elements.set(id, {value:'', files:[], checked:false, dataset:{}, options:[],
    addEventListener(type, fn) { this[type] = fn; },
    replaceChildren() {}, append() {}, contains() { return false; }});
  return elements.get(id);
}
let source = fs.readFileSync(__dirname + '/dashboard.html', 'utf8').split('<script>')[1].split('</script>')[0];
source = source.replace(/poll\(\);\s*$/, '');
const context = vm.createContext({console, Intl, document:{getElementById:element, createElement:()=>({append(){},addEventListener(){}})},
  window:{addEventListener(){}, confirm(){throw Error('Unexpected flash confirmation');}},
  Option:function(){}, setTimeout(){}, fetch:async()=>{throw Error('Unexpected request');}});
vm.runInContext(source, context);
async function run(code) { return await vm.runInContext(code, context); }
(async () => {
  await run(`status = {firmware_image_safety:1, devices:[{address:'unit',wsup_service:true}],
    device_info:{address:'unit',protocol:'wsup',supported:true}, images:{esphome:{filename:'Kewe.bin',sha256:'old',bytes:3}}};
    selected='unit'; online=true; byId('update-protocol').value='auto'; byId('network-verified').checked=true; updateControls();`);
  assert.equal(element('flash').disabled, false);
  element('esphome-file').files = [{name:'Cardiff.bin',size:3,arrayBuffer:async()=>new ArrayBuffer(3)}];
  element('esphome-file').change();
  assert.equal(element('flash').disabled, true);
  assert.match(element('image-selection').textContent, /NOT LOADED: Cardiff.bin/);
  await run(`confirmFlash('esphome');`); // Must neither confirm nor POST stale Kewe image.
  context.fetch = async()=>({ok:false,status:400,text:async()=>'Invalid image'});
  await run('loadEsphomeImage()');
  assert.equal(element('flash').disabled, true);
  assert.equal(element('esphome-file').files[0].name, 'Cardiff.bin');
  assert.match(element('error').textContent, /Image load failed/);
  context.fetch = async()=>({ok:true,json:async()=>({firmware_image_safety:1,devices:[{address:'unit',wsup_service:true}],
    device_info:{address:'unit',protocol:'wsup',supported:true},images:{esphome:{filename:'Cardiff.bin',sha256:'new',bytes:3}}})});
  Object.defineProperty(element('esphome-file'), 'value', {set(v){if(v==='')this.files=[];}});
  await run('loadEsphomeImage()');
  assert.equal(element('flash').disabled, false);
  assert.match(element('esphome-info').textContent, /SERVER-LOADED IMAGE: Cardiff.bin/);
  await run('delete status.firmware_image_safety; updateControls(); confirmFlash("esphome");');
  assert.equal(element('flash').disabled, true);
  assert.equal(element('restore').disabled, true);
  assert.equal(element('image-server-warning').hidden, false);
  console.log('Dashboard image selection, failed/successful loading, and old-server guards passed.');
})().catch(error=>{console.error(error);process.exitCode=1;});
