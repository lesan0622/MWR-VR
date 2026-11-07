import { useEffect, useRef, useState } from "react";
import { bridge } from "./bridge";
import { errorKey } from "./i18n";
import type { Notice } from "./useSettings";
import { languageAvailable, languageBlocksLaunch, languageSelectValue, type GameLanguageResult as Result } from "./gameLanguageModel";

export function useGameLanguages(enabled: boolean) {
  const [result, setResult] = useState<Result | null>(null);
  const [busy, setBusy] = useState(false);
  const [notice, setNotice] = useState<Notice | null>(null);
  const busyRef = useRef(false);
  const generation = useRef(0);
  const live = useRef(false);
  async function run(language?: string) {
    if (busyRef.current || !enabled) return;
    busyRef.current = true;
    setBusy(true);
    setNotice({
      key: language ? "language.gameSaving" : "language.gameScanning",
    });
    const current = ++generation.current;
    try {
      let response = await bridge!.request<Result>(
        language ? "languages.save" : "languages.scan",
        language ? { language } : {},
      );
      while (
        response.pending &&
        live.current &&
        current === generation.current
      ) {
        await new Promise((resolve) => setTimeout(resolve, 100));
        if (!live.current || current !== generation.current) return;
        response = await bridge!.request<Result>("languages.poll");
      }
      if (!live.current || current !== generation.current) return;
      setResult(response);
      const available = languageAvailable(response);
      setNotice(
        response.managedBySteam
          ? { key: "language.gameSteam" }
          : response.saved
          ? { key: "language.gameSaved" }
          : !response.choices.length
            ? { key: "language.gameEmpty", error: true }
            : !available
              ? { key: "language.gameCurrentUnavailable", error: true }
              : null,
      );
    } catch (error) {
      if (live.current && current === generation.current)
        setNotice({ key: errorKey(error), error: true });
    } finally {
      if (live.current && current === generation.current) {
        busyRef.current = false;
        setBusy(false);
      }
    }
  }
  useEffect(() => {
    live.current = true;
    void run();
    return () => {
      live.current = false;
      generation.current++;
      busyRef.current = false;
    };
  }, [enabled]);
  return {
    result,
    busy,
    blocksLaunch: languageBlocksLaunch(result),
    selectValue: languageSelectValue(result),
    notice,
    scan: () => run(),
    save: (language: string) => run(language),
    available: result
      ? result.choices.some((choice) => choice.value === result.language)
      : null,
  };
}
