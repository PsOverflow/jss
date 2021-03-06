/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

package org.mozilla.jss.pkix.crmf;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.mozilla.jss.asn1.ASN1Template;
import org.mozilla.jss.asn1.ASN1Value;
import org.mozilla.jss.asn1.CHOICE;
import org.mozilla.jss.asn1.EXPLICIT;
import org.mozilla.jss.asn1.InvalidBERException;
import org.mozilla.jss.asn1.NULL;
import org.mozilla.jss.asn1.Tag;
import org.mozilla.jss.util.Assert;

/**
 * CRMF <i>ProofOfPossession</i>:
 * <pre>
 * ProofOfPossession ::= CHOICE {
 *      raVerified          [0] NULL,
 *      signature           [1] POPOSigningKey,
 *      keyEncipherment     [2] POPOPrivKey,
 *      keyAgreement        [3] POPOPrivKey }
 * </pre>
 */
public class ProofOfPossession implements ASN1Value {

    /**
     * The type of ProofOfPossesion.
     */
    public static class Type {
        private Type() { }

        static Type RA_VERIFIED = new Type();
        static Type SIGNATURE = new Type();
        static Type KEY_ENCIPHERMENT = new Type();
        static Type KEY_AGREEMENT = new Type();
    }
    public static Type RA_VERIFIED = Type.RA_VERIFIED;
    public static Type SIGNATURE = Type.SIGNATURE;
    public static Type KEY_ENCIPHERMENT = Type.KEY_ENCIPHERMENT;
    public static Type KEY_AGREEMENT = Type.KEY_AGREEMENT;

    ///////////////////////////////////////////////////////////////////////
    // members and member access
    ///////////////////////////////////////////////////////////////////////

    private Type type;
    private POPOSigningKey signature; // if type == SIGNATURE
    private POPOPrivKey keyEncipherment; // if type == KEY_ENCIPHERMENT
    private POPOPrivKey keyAgreement; // if type == KEY_AGREEMENT

    /**
     * Returns the type of ProofOfPossesion: <ul>
     * <li><code>RA_VERIFIED</code>
     * <li><code>SIGNATURE</code>
     * <li><code>KEY_ENCIPHERMENT</code>
     * <li><code>KEY_AGREEMENT</code>
     * </ul>
     */
    public Type getType() {
        return type;
    }

    /**
     * If type == SIGNATURE, returns the signature field. Otherwise,
     * returns null.
     */
    public POPOSigningKey getSignature() {
        return signature;
    }

    /**
     * If type == KEY_ENCIPHERMENT, returns the keyEncipherment field.
     * Otherwise, returns null.
     */
    public POPOPrivKey getKeyEncipherment() {
        return keyEncipherment;
    }

    /**
     * If type == KEY_AGREEMENT, returns the keyAgreement field. Otherwise,
     * returns null.
     */
    public POPOPrivKey getKeyAgreement() {
        return keyAgreement;
    }

    ///////////////////////////////////////////////////////////////////////
    // Constructors
    ///////////////////////////////////////////////////////////////////////

    private ProofOfPossession() { }

    private ProofOfPossession(Type type, POPOSigningKey signature,
                POPOPrivKey keyEncipherment, POPOPrivKey keyAgreement) {
        this.type = type;
        this.signature = signature;
        this.keyEncipherment = keyEncipherment;
        this.keyAgreement = keyAgreement;
    }

    /**
     * Creates a new ProofOfPossesion with an raVerified field.
     */
    public static ProofOfPossession
    createRaVerified() {
        return new ProofOfPossession( RA_VERIFIED, null, null, null );
    }

    /**
     * Creates a new ProofOfPossesion with the given signature field.
     */
    public static ProofOfPossession
    createSignature(POPOSigningKey signature) {
        return new ProofOfPossession( SIGNATURE, signature, null, null );
    }

    /**
     * Creates a new ProofOfPossesion with the given keyEncipherment field.
     */
    public static ProofOfPossession
    createKeyEncipherment(POPOPrivKey keyEncipherment) {
        return new ProofOfPossession(
            KEY_ENCIPHERMENT, null, keyEncipherment, null );
    }

    /**
     * Creates a new ProofOfPossesion with the given keyAgreement field.
     */
    public static ProofOfPossession
    createKeyAgreement(POPOPrivKey keyAgreement) {
        return new ProofOfPossession(
            KEY_AGREEMENT, null, null, keyAgreement );
    }

    ///////////////////////////////////////////////////////////////////////
    // decoding/encoding
    ///////////////////////////////////////////////////////////////////////


    public Tag getTag() {
        if( type == RA_VERIFIED ) {
            return Tag.get(0);
        } else if( type == SIGNATURE ) {
            return Tag.get(1);
        } else if( type == KEY_ENCIPHERMENT ) {
            return Tag.get(2);
        } else {
            assert( type == KEY_AGREEMENT );
            return Tag.get(3);
        }
    }

    public void encode(OutputStream ostream) throws IOException {

        if( type == RA_VERIFIED ) {
            (new NULL()).encode(Tag.get(0), ostream);
        } else if( type == SIGNATURE ) {
            signature.encode(Tag.get(1), ostream);
        } else if( type == KEY_ENCIPHERMENT ) {
            // a CHOICE must be explicitly tagged
            EXPLICIT e = new EXPLICIT( Tag.get(2), keyEncipherment );
            e.encode(ostream);
        } else {
            assert( type == KEY_AGREEMENT );
            // a CHOICE must be explicitly tagged
            EXPLICIT e = new EXPLICIT( Tag.get(3), keyAgreement );
            e.encode(ostream);
        }
    }

    public void encode(Tag implicitTag, OutputStream ostream)
            throws IOException {
        assert(implicitTag.equals(getTag()));
        encode(ostream);
    }

    /**
     * A Template for decoding a ProofOfPossession.
     */
    public static class Template implements ASN1Template {

        private CHOICE.Template choicet;

        public Template() {
            choicet = new CHOICE.Template();

            choicet.addElement( Tag.get(0), NULL.getTemplate() );
            choicet.addElement( Tag.get(1), POPOSigningKey.getTemplate() );
            EXPLICIT.Template et = new EXPLICIT.Template(
                Tag.get(2), POPOPrivKey.getTemplate() );
            choicet.addElement( et );
            et = new EXPLICIT.Template(
                Tag.get(3), POPOPrivKey.getTemplate() );
            choicet.addElement( et );
        }

        public boolean tagMatch(Tag tag) {
            return choicet.tagMatch(tag);
        }

        public ASN1Value decode(InputStream istream)
                throws InvalidBERException, IOException {
            CHOICE c = (CHOICE) choicet.decode(istream);

            if( c.getTag().equals(Tag.get(0)) ) {
                return createRaVerified();
            } else if( c.getTag().equals(Tag.get(1)) ) {
                return createSignature( (POPOSigningKey) c.getValue() );
            } else if( c.getTag().equals(Tag.get(2)) ) {
                EXPLICIT e = (EXPLICIT) c.getValue();
                return createKeyEncipherment( (POPOPrivKey) e.getContent() );
            } else {
                assert( c.getTag().equals(Tag.get(3)) );
                EXPLICIT e = (EXPLICIT) c.getValue();
                return createKeyAgreement( (POPOPrivKey) e.getContent() );
            }
        }

        public ASN1Value decode(Tag implicitTag, InputStream istream)
                throws InvalidBERException, IOException {
            throw new RuntimeException("A CHOICE cannot be implicitly tagged");
            // return decode(istream);
        }
    }
}
