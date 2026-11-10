import assert from "node:assert/strict";
import test from "node:test";
import {guideSteps, guideFields} from "../src/onboarding";
import {collect, makeDraft, fields, type SettingsState} from "../src/model";

test("all six onboarding steps save supported settings, including Visual Comfort", () => {
  const state = {values:{}, limits:{}, choices:{}} as unknown as SettingsState;
  assert.equal(guideSteps.length, 6);
  assert.equal(guideFields.length, 6);
  for(const name of guideFields.flat()) {
    const field=fields.find(f=>f.name===name);
    assert.ok(field, `${name} must exist in the settings schema`);
    if(field.type==="toggle")state.values[name]=false;
    else if(field.type==="number") {
      state.values[name]=.3;
      state.limits[name]={min:0,max:1,step:.01,displayScale:1};
    } else {
      state.values[name]="right";
      state.choices[name]=[{value:"right",labelKey:""},{value:"left",labelKey:""}];
    }
  }
  const draft=makeDraft(state.values,state.limits);
  for(const keys of guideFields)assert.doesNotThrow(()=>collect(draft,state,state.values,keys));
  assert.ok(guideFields[1].includes("vr_controlHand"));
  assert.ok(guideFields[1].includes("vr_pistolHolsterSide"));
  draft.vr_controlHand="left";
  draft.vr_pistolHolsterSide="right";
  const values=collect(draft,state,state.values,guideFields[1]);
  assert.equal(values.vr_controlHand,"left");
  assert.equal(values.vr_pistolHolsterSide,"right");
});
