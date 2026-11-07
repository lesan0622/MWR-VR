export interface GameLanguageResult {
  ok?: boolean;
  pending?: boolean;
  language: string;
  choices: Array<{ value: string; label: string }>;
  saved?: boolean;
  managedBySteam?: boolean;
}

export function languageAvailable(result: GameLanguageResult): boolean {
  return result.choices.some((choice) => choice.value === result.language);
}

export function languageBlocksLaunch(result: GameLanguageResult | null): boolean {
  return !!result && !result.managedBySteam && !languageAvailable(result);
}

export function languageSelectValue(result: GameLanguageResult | null): string {
  // A value absent from <option>s makes the browser display another pack.
  return result && languageAvailable(result) ? result.language : "";
}
