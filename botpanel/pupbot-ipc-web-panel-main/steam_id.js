const steamid64_base = '76561197960265728';

function clean_uint_string(value) {
    if (value === null || value === undefined)
        return null;

    const text = String(value).trim();
    if (!/^\d+$/.test(text))
        return null;

    return text.replace(/^0+(?=\d)/, '');
}

function add_decimal_strings(left, right) {
    var carry = 0;
    var result = '';
    var left_index = left.length - 1;
    var right_index = right.length - 1;

    while (left_index >= 0 || right_index >= 0 || carry) {
        const left_digit = left_index >= 0 ? left.charCodeAt(left_index--) - 48 : 0;
        const right_digit = right_index >= 0 ? right.charCodeAt(right_index--) - 48 : 0;
        const sum = left_digit + right_digit + carry;
        result = String(sum % 10) + result;
        carry = Math.floor(sum / 10);
    }

    return result;
}

function subtract_decimal_strings(left, right) {
    left = clean_uint_string(left);
    right = clean_uint_string(right);
    if (!left || !right)
        return null;
    if (left.length < right.length || (left.length === right.length && left < right))
        return null;

    var borrow = 0;
    var result = '';
    var left_index = left.length - 1;
    var right_index = right.length - 1;

    while (left_index >= 0) {
        var left_digit = left.charCodeAt(left_index--) - 48 - borrow;
        const right_digit = right_index >= 0 ? right.charCodeAt(right_index--) - 48 : 0;
        borrow = 0;
        if (left_digit < right_digit) {
            left_digit += 10;
            borrow = 1;
        }

        result = String(left_digit - right_digit) + result;
    }

    return result.replace(/^0+(?=\d)/, '');
}

function account_id32_to_steamid64(account_id32) {
    const account_id = clean_uint_string(account_id32);
    if (!account_id || account_id === '0')
        return null;

    return add_decimal_strings(steamid64_base, account_id);
}

function steamid64_to_account_id32(steamid64) {
    const steam_id = clean_uint_string(steamid64);
    if (!steam_id)
        return null;

    const account_id = subtract_decimal_strings(steam_id, steamid64_base);
    return account_id && account_id !== '0' ? account_id : null;
}

function profile_url_from_account_id32(account_id32) {
    const steamid64 = account_id32_to_steamid64(account_id32);
    return steamid64 ? `https://steamcommunity.com/profiles/${steamid64}` : null;
}

module.exports = {
    account_id32_to_steamid64,
    steamid64_to_account_id32,
    profile_url_from_account_id32
};
