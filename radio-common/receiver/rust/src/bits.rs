// SPDX-FileCopyrightText: 2026 The Yantra contributors
// SPDX-License-Identifier: GPL-3.0-or-later

//! Bit reader over unpacked bits (one bit per byte, first bit first), the
//! form in which the lower MAC hands decoded blocks over. Every read names
//! its field; with tracing on, the reader records each field (offset,
//! width, raw value, meaning) for the field-by-field decode view (DEC-04).

use std::fmt;

/// Why a parser stopped.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Error {
    /// The input ended inside a field.
    Truncated { field: &'static str, offset: usize, need: usize, have: usize },
    /// A field holds a value the PDU does not allow.
    Invalid { field: &'static str, value: u64 },
    /// A valid construct this crate does not parse (yet).
    Unsupported { what: &'static str },
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Error::Truncated { field, offset, need, have } => write!(
                f,
                "truncated: {field} at bit {offset} needs {need} bits, {have} left"
            ),
            Error::Invalid { field, value } => write!(f, "invalid {field}: {value}"),
            Error::Unsupported { what } => write!(f, "not supported: {what}"),
        }
    }
}

impl std::error::Error for Error {}

pub type Result<T> = std::result::Result<T, Error>;

/// One decoded field of the field-by-field view.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Field {
    /// Bit offset in the input.
    pub offset: usize,
    pub width: usize,
    /// Field name, prefixed with the element it belongs to (e.g.
    /// "neighbour 2: main carrier number").
    pub name: String,
    /// The value as sent; `None` for fields that are skipped without being
    /// shown (subscriber addresses, spec 16.5).
    pub raw: Option<u64>,
    /// What the value means (empty when the raw value says it all).
    pub meaning: String,
}

/// Reads named fields MSB first from unpacked bits.
pub struct BitReader<'a> {
    bits: &'a [u8],
    pos: usize,
    end: usize,
    trace: Option<Vec<Field>>,
    scope: String,
}

impl<'a> BitReader<'a> {
    /// A reader over `bits` (any non-zero byte is a 1 bit), without trace.
    pub fn new(bits: &'a [u8]) -> Self {
        BitReader { bits, pos: 0, end: bits.len(), trace: None, scope: String::new() }
    }

    /// A reader that records every field it reads.
    pub fn traced(bits: &'a [u8]) -> Self {
        BitReader { trace: Some(Vec::new()), ..BitReader::new(bits) }
    }

    pub fn pos(&self) -> usize {
        self.pos
    }

    /// End of the readable range (exclusive).
    pub fn end(&self) -> usize {
        self.end
    }

    pub fn remaining(&self) -> usize {
        self.end - self.pos
    }

    /// Moves the read position to `pos` (at most the end).
    pub fn seek(&mut self, pos: usize) {
        self.pos = pos.min(self.end);
    }

    /// Limits reading to bits before `end` (never beyond the input).
    pub fn set_end(&mut self, end: usize) {
        self.end = end.min(self.bits.len()).max(self.pos);
    }

    /// True when fields are recorded.
    pub fn tracing(&self) -> bool {
        self.trace.is_some()
    }

    /// Prefix for the names of the following fields ("" for none).
    pub fn set_scope(&mut self, scope: &str) {
        self.scope.clear();
        self.scope.push_str(scope);
    }

    /// The recorded fields.
    pub fn into_trace(self) -> Vec<Field> {
        self.trace.unwrap_or_default()
    }

    fn raw_bits(&self, at: usize, width: usize) -> u64 {
        self.bits[at..at + width]
            .iter()
            .fold(0u64, |v, &b| (v << 1) | u64::from(b != 0))
    }

    fn need(&self, field: &'static str, width: usize) -> Result<()> {
        if width > self.remaining() {
            return Err(Error::Truncated { field, offset: self.pos, need: width, have: self.remaining() });
        }
        Ok(())
    }

    /// The next `width` bits (at most 64) without moving on.
    pub fn peek(&self, field: &'static str, width: usize) -> Result<u64> {
        debug_assert!(width <= 64);
        self.need(field, width)?;
        Ok(self.raw_bits(self.pos, width))
    }

    /// Reads a field of `width` bits (at most 64).
    pub fn read(&mut self, field: &'static str, width: usize) -> Result<u64> {
        let v = self.peek(field, width)?;
        if let Some(t) = self.trace.as_mut() {
            t.push(Field {
                offset: self.pos,
                width,
                name: format!("{}{}", self.scope, field),
                raw: Some(v),
                meaning: String::new(),
            });
        }
        self.pos += width;
        Ok(v)
    }

    pub fn read_u8(&mut self, field: &'static str, width: usize) -> Result<u8> {
        debug_assert!(width <= 8);
        Ok(self.read(field, width)? as u8)
    }

    pub fn read_u16(&mut self, field: &'static str, width: usize) -> Result<u16> {
        debug_assert!(width <= 16);
        Ok(self.read(field, width)? as u16)
    }

    pub fn read_u32(&mut self, field: &'static str, width: usize) -> Result<u32> {
        debug_assert!(width <= 32);
        Ok(self.read(field, width)? as u32)
    }

    /// Reads a one-bit flag.
    pub fn flag(&mut self, field: &'static str) -> Result<bool> {
        Ok(self.read(field, 1)? != 0)
    }

    /// Skips `width` bits whose value is never shown (an address, spec
    /// 16.5); the trace records only that they are there and `meaning`.
    pub fn skip_hidden(&mut self, field: &'static str, width: usize, meaning: &str) -> Result<()> {
        self.need(field, width)?;
        if let Some(t) = self.trace.as_mut() {
            t.push(Field {
                offset: self.pos,
                width,
                name: format!("{}{}", self.scope, field),
                raw: None,
                meaning: meaning.to_string(),
            });
        }
        self.pos += width;
        Ok(())
    }

    /// Sets the meaning of the field read last; `text` runs only when
    /// tracing, so plain decoding pays nothing for it.
    pub fn mean<F: FnOnce() -> String>(&mut self, text: F) {
        if let Some(last) = self.trace.as_mut().and_then(|t| t.last_mut()) {
            last.meaning = text();
        }
    }

    /// Records a marker line (no bits) in the trace, e.g. an element
    /// heading or why decoding stopped.
    pub fn note<F: FnOnce() -> String>(&mut self, name: &'static str, text: F) {
        if let Some(t) = self.trace.as_mut() {
            t.push(Field {
                offset: self.pos,
                width: 0,
                name: format!("{}{}", self.scope, name),
                raw: None,
                meaning: text(),
            });
        }
    }

    /// The input bits `start..end` (clamped to the input).
    pub fn slice(&self, start: usize, end: usize) -> &'a [u8] {
        let end = end.min(self.bits.len());
        &self.bits[start.min(end)..end]
    }

    /// Index of the last 1 bit before `end` at or after `start`, if any.
    pub fn last_one(&self, start: usize, end: usize) -> Option<usize> {
        let end = end.min(self.bits.len());
        (start..end).rev().find(|&i| self.bits[i] != 0)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_msb_first_and_counts_position() {
        let bits = [1u8, 0, 1, 1, 0, 0, 0, 1, 1];
        let mut r = BitReader::new(&bits);
        assert_eq!(r.read("a", 3).unwrap(), 0b101);
        assert_eq!(r.read("b", 5).unwrap(), 0b10001);
        assert_eq!(r.pos(), 8);
        assert!(r.flag("c").unwrap());
        assert_eq!(r.remaining(), 0);
    }

    #[test]
    fn non_zero_bytes_are_one_bits() {
        let bits = [0xFFu8, 0, 2];
        assert_eq!(BitReader::new(&bits).read("x", 3).unwrap(), 0b101);
    }

    #[test]
    fn truncation_names_the_field_and_keeps_the_position() {
        let bits = [1u8; 5];
        let mut r = BitReader::new(&bits);
        r.read("first", 3).unwrap();
        let e = r.read("second", 4).unwrap_err();
        assert_eq!(e, Error::Truncated { field: "second", offset: 3, need: 4, have: 2 });
        assert_eq!(r.pos(), 3);
        assert!(e.to_string().contains("second"));
    }

    #[test]
    fn end_limit_and_seek() {
        let bits = [1u8; 10];
        let mut r = BitReader::new(&bits);
        r.set_end(4);
        assert!(r.read("x", 5).is_err());
        r.seek(2);
        assert_eq!(r.remaining(), 2);
        r.set_end(100);
        assert_eq!(r.end(), 10);
    }

    #[test]
    fn trace_records_fields_meanings_and_hidden_values() {
        let bits = [1u8, 1, 1, 1, 0, 1];
        let mut r = BitReader::traced(&bits);
        r.set_scope("el: ");
        r.read("two", 2).unwrap();
        r.mean(|| "three".into());
        r.skip_hidden("addr", 2, "broadcast").unwrap();
        r.skip_hidden("addr2", 2, "an address").unwrap();
        let t = r.into_trace();
        assert_eq!(t.len(), 3);
        assert_eq!(t[0].name, "el: two");
        assert_eq!(t[0].raw, Some(3));
        assert_eq!(t[0].meaning, "three");
        assert_eq!(t[1].raw, None);
        assert_eq!(t[1].meaning, "broadcast");
        assert_eq!(t[2].meaning, "an address");
    }

    #[test]
    fn untraced_reader_never_builds_meanings() {
        let bits = [1u8];
        let mut r = BitReader::new(&bits);
        r.read("x", 1).unwrap();
        r.mean(|| panic!("must not run"));
        assert!(r.into_trace().is_empty());
    }

    #[test]
    fn last_one_finds_the_fill_start() {
        let bits = [0u8, 1, 0, 1, 0, 0];
        let r = BitReader::new(&bits);
        assert_eq!(r.last_one(0, 6), Some(3));
        assert_eq!(r.last_one(4, 6), None);
    }
}
