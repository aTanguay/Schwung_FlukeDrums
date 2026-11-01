// Preview how Schwung lays FlukeDrums out on the knob grid, using Schwung's
// own page planner. Run after changing build_ui_hierarchy / chain_params.
//
//   git clone --depth 1 https://github.com/charlesvestal/schwung ../schwung
//   # get the module's live contracts: the workflow test writes them to
//   #   <TEST_ROOT>/ui_hierarchy.json and <TEST_ROOT>/chain_params.json
//   node scripts/preview-pages.mjs ../schwung <ui_hierarchy.json> <chain_params.json>
import { readFileSync } from 'fs';
import { resolve } from 'path';
import { pathToFileURL } from 'url';

const [schwungDir, hierFile, cpFile] = process.argv.slice(2);
if (!schwungDir || !hierFile || !cpFile) {
  console.error('usage: node scripts/preview-pages.mjs <schwung-checkout> <ui_hierarchy.json> <chain_params.json>');
  process.exit(2);
}
const planner = pathToFileURL(resolve(schwungDir, 'src/shared/param_pages/page_plan.mjs')).href;
const { planPages } = await import(planner);
const res = planPages({
  hierarchy: JSON.parse(readFileSync(hierFile, 'utf8')),
  chainParams: JSON.parse(readFileSync(cpFile, 'utf8')),
});
if (res.warnings && res.warnings.length) console.log('warnings:', res.warnings);
res.pages.forEach((p, i) => {
  const keys = (p.keys || []).map((k) => (k == null ? '.' : k));
  console.log(`${String(i + 1).padStart(2)}. [${p.kind}] ${p.name || ''}`);
  if (keys.length) console.log('      top:    ' + keys.slice(0, 4).join(' | '));
  if (keys.length > 4) console.log('      bottom: ' + keys.slice(4, 8).join(' | '));
});
