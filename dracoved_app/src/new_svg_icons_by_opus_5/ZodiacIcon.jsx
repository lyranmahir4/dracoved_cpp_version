const P = {
  aries: ["M12 20 C12 12 10 5 7 5 C4.5 5 4 8 5 9.5", "M12 20 C12 12 14 5 17 5 C19.5 5 20 8 19 9.5"],
  taurus: ["M5.5 6 A6.5 6.5 0 0 0 18.5 6", "M12 11 A5 5 0 1 0 12 21 A5 5 0 1 0 12 11"],
  gemini: ["M6 4.5 C9 6.5 15 6.5 18 4.5", "M6 19.5 C9 17.5 15 17.5 18 19.5", "M9 5.6 V18.4", "M15 5.6 V18.4"],
  cancer: ["M3.5 12 C3.5 7 7.5 4.5 12 4.5 C15 4.5 17.5 6 18.6 8.2", "M18.6 8.3 A2.3 2.3 0 1 1 18.6 12.9 A2.3 2.3 0 1 1 18.6 8.3", "M20.5 12 C20.5 17 16.5 19.5 12 19.5 C9 19.5 6.5 18 5.4 15.8", "M5.4 11.1 A2.3 2.3 0 1 1 5.4 15.7 A2.3 2.3 0 1 1 5.4 11.1"],
  leo: ["M4.3 16 A3.2 3.2 0 1 1 10.7 16 A3.2 3.2 0 1 1 4.3 16", "M9.5 13.3 C7 10.5 6.5 6.5 9.5 5.3 C12.5 4.1 14.5 7.5 13.5 10.5 C12.5 13.5 13 16 15.5 17 C17.5 17.8 19 17 19.5 15.5"],
  virgo: ["M5 8.5 V17", "M5 8.5 C5 6 8 6 8 8.5 V17", "M8 8.5 C8 6 11 6 11 8.5 V17", "M11 8.5 C11 6 14 6 14 8.5 V14.5 C14 18 16.5 20 19 18.5 C21 17.3 20.6 14.2 18.2 14.2 C16.4 14.2 15.2 16 16.5 18.2"],
  libra: ["M4 18.5 H20", "M4 13.5 H8", "M8 13.5 A4.2 4.2 0 0 1 16 13.5", "M16 13.5 H20"],
  scorpio: ["M5 8.5 V17", "M5 8.5 C5 6 8 6 8 8.5 V17", "M8 8.5 C8 6 11 6 11 8.5 V17", "M11 8.5 C11 6 14 6 14 8.5 V18 L19.5 12.5", "M19.5 12.5 H15.3", "M19.5 12.5 V16.7"],
  sagittarius: ["M4.5 19.5 L18.5 5.5", "M18.5 5.5 H13", "M18.5 5.5 V11", "M6.5 11.5 L12.5 17.5"],
  capricorn: ["M4.5 6.5 L8.5 15.5 L11 8 C11.5 6.5 13 6 14.5 6.5 C17.5 7.5 18.8 10.5 18 13.5 C17.3 16.2 15 17.8 13 16.8 C11.4 16 11.4 13.8 13 13.2"],
  aquarius: ["M4 10.5 L7 7.5 L10 10.5 L13 7.5 L16 10.5 L19 7.5", "M4 17.5 L7 14.5 L10 17.5 L13 14.5 L16 17.5 L19 14.5"],
  pisces: ["M8 4.5 C4.5 8 4.5 16 8 19.5", "M16 4.5 C19.5 8 19.5 16 16 19.5", "M5 12 H19"],
};

export const ZODIAC_SIGNS = Object.keys(P);

export default function ZodiacIcon({ sign, size = 24, strokeWidth = 1.5, title, ...rest }) {
  const paths = P[sign];
  if (!paths) return null;
  return (
    <svg
      xmlns="http://www.w3.org/2000/svg"
      width={size}
      height={size}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      strokeWidth={strokeWidth}
      strokeLinecap="round"
      strokeLinejoin="round"
      role={title ? "img" : undefined}
      aria-hidden={title ? undefined : true}
      {...rest}
    >
      {title ? <title>{title}</title> : null}
      {paths.map((d, i) => <path key={i} d={d} />)}
    </svg>
  );
}
